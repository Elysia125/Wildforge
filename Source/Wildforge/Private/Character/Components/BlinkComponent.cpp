// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/BlinkComponent.h"

#include "Animation/AnimInstance.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"
#include "Utils/ComponentAuthorityGuard.h"
#include "Utils/WildforgeLog.h"

// ===== 点按闪现 =====
//
// 全部位移都在**权威端**做：
//   * 客户端自己 SetActorLocation 会被服务器用位置校验回滚（而且能穿墙）；
//   * 客户端的请求里只带「距离」这一个意图，方向与落点完全由服务器根据
//     复制下来的朝向 + 自己的碰撞世界算出来。
//
// 这个分工是刻意的：闪现是**最容易变成穿墙外挂**的能力，
// 所以「往哪闪、能闪多远、落在哪」必须只有一个决策者。

UBlinkComponent::UBlinkComponent() {
  // 闪现是纯事件驱动（RPC → 位移 → Multicast），不需要 Tick
  PrimaryComponentTick.bCanEverTick = false;

  // 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）。
  // 缺了这行 DOREPLIFETIME_* 等于没写。
  SetIsReplicatedByDefault(true);
}

void UBlinkComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 「服务器写、客户端只读」，COND_OwnerOnly：只有本人需要看到自己的闪现冷却与次数。
  // ⚠️ 不要改成 COND_SimulatedOnly——那个条件不会发给自主代理（本地玩家自己）。
  DOREPLIFETIME_CONDITION(UBlinkComponent, LastBlinkTime, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(UBlinkComponent, BlinkCount, COND_OwnerOnly);
}

bool UBlinkComponent::IsBlinkReady() const {
  if (BlinkCooldown <= 0.0f) {
    return true;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  return (Now - LastBlinkTime) >= BlinkCooldown;
}

float UBlinkComponent::GetBlinkCooldownRemaining() const {
  if (BlinkCooldown <= 0.0f || !FMath::IsFinite(LastBlinkTime)) {
    return 0.0f;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  return FMath::Max(0.0f, BlinkCooldown - (Now - LastBlinkTime));
}

FString UBlinkComponent::GetBlinkDebugString() const {
  const AActor *Owner = GetOwner();
  return FString::Printf(
      TEXT("bCanBlink=%d 上限=%.1fcm 冷却=%.2fs（剩余 %.2fs）累计=%d 上次=%.2fs "
           "保留速度=%d 通道=%d 宿主位置=%.1f,%.1f,%.1f"),
      bCanBlink ? 1 : 0, MaxBlinkDistance, BlinkCooldown,
      GetBlinkCooldownRemaining(), BlinkCount,
      FMath::IsFinite(LastBlinkTime) ? LastBlinkTime : -1000.0f,
      bKeepVelocityAfterBlink ? 1 : 0, static_cast<int32>(BlinkTraceChannel.GetValue()),
      Owner ? Owner->GetActorLocation().X : 0.0f,
      Owner ? Owner->GetActorLocation().Y : 0.0f,
      Owner ? Owner->GetActorLocation().Z : 0.0f);
}

bool UBlinkComponent::BlinkForward_Implementation(
    FVector &OutLandingLocation, float Distance, float MaxDistance,
    bool bKeepVelocity, ECollisionChannel TraceChannel) {
  AActor *Owner = GetOwner();
  OutLandingLocation = Owner ? Owner->GetActorLocation() : FVector::ZeroVector;

  WF_COMPONENT_AUTHORITY_GUARD(false);

  const FString Who = Owner ? Owner->GetName() : TEXT("None");

  if (!FMath::IsFinite(Distance) || Distance <= 0.0f ||
      !FMath::IsFinite(MaxDistance)) {
    WFLOG_WARNING("[闪现] 被拒：距离参数非法（Distance=%.2f, MaxDistance=%.2f）。"
                  "宿主 %s",
                  Distance, MaxDistance, *Who);
    return false;
  }

  // 服务器收敛：客户端可以要更远，但最多只给 MaxDistance / MaxBlinkDistance 里更小的那个
  const float ServerCap = FMath::Min(MaxDistance, MaxBlinkDistance);
  const float RequestedDistance = FMath::Min(Distance, FMath::Max(ServerCap, 0.0f));
  if (RequestedDistance <= 0.0f) {
    WFLOG_WARNING("[闪现] 被拒：收敛后的距离为 0（Distance=%.2f, MaxDistance=%.2f, "
                  "组件上限=%.2f）。宿主 %s",
                  Distance, MaxDistance, MaxBlinkDistance, *Who);
    return false;
  }

  UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  if (Movement == nullptr) {
    WFLOG_WARNING("[闪现] 被拒：宿主 %s 上没有 CharacterMovementComponent。",
                  *Who);
    return false;
  }

  const UCapsuleComponent *Capsule =
      Owner ? Owner->FindComponentByClass<UCapsuleComponent>() : nullptr;
  if (Capsule == nullptr) {
    WFLOG_WARNING("[闪现] 被拒：宿主 %s 上没有胶囊体组件。", *Who);
    return false;
  }

  const UWorld *World = GetWorld();
  if (World == nullptr) {
    WFLOG_WARNING("[闪现] 被拒：World 为空。宿主 %s", *Who);
    return false;
  }

  // 方向取视线水平分量：忽略俯仰角，朝上不会顶天花板、朝下不会钻地
  FVector ViewLocation;
  FRotator ViewRotation;
  Owner->GetActorEyesViewPoint(ViewLocation, ViewRotation);

  FVector DashDirection = ViewRotation.Vector();
  DashDirection.Z = 0.0;
  if (!DashDirection.Normalize()) {
    // 视线完全竖直（或没有 Controller）时退回角色自身前向
    WFLOG_INFO("[闪现] 视线无法给出水平方向（俯仰=%.1f°），改用角色前向。宿主 %s",
               ViewRotation.Pitch, *Who);
    DashDirection = Owner->GetActorForwardVector();
    DashDirection.Z = 0.0;
    if (!DashDirection.Normalize()) {
      WFLOG_WARNING("[闪现] 被拒：视线与角色前向都无法给出水平方向。宿主 %s",
                    *Who);
      return false;
    }
  }

  // 胶囊体半高在半蹲时会变小，查询形状要跟着走，否则会误判「钻得过去」
  const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
  const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
  const FCollisionShape CapsuleShape =
      FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);

  // 忽略自身，避免扫描一开始就撞到自己的胶囊体
  FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BlinkForward), false, Owner);
  // 挂在宿主身上的子 Actor（手里的武器等）也不该挡自己
  QueryParams.AddIgnoredActors(Owner->Children);

  const FVector StartLocation = Owner->GetActorLocation();
  FVector DesiredLocation = StartLocation + DashDirection * RequestedDistance;
  const float WalkableFloorZ = Movement->GetWalkableFloorZ();

  // 先探一眼整段位移有没有障碍；有的话按障碍法线把落点贴回可站立面，再重扫一次
  FHitResult ProbeHit;
  if (World->SweepSingleByChannel(ProbeHit, StartLocation, DesiredLocation,
                                  FQuat::Identity, TraceChannel, CapsuleShape,
                                  QueryParams)) {
    const bool bWalkableSurface = ProbeHit.ImpactNormal.Z >= WalkableFloorZ;

    WFLOG_INFO("[闪现] 整段扫描有阻挡：%s（法线 Z=%.3f，可站立阈值 %.3f，"
               "命中距离 %.1fcm）。宿主 %s",
               ProbeHit.GetActor() ? *ProbeHit.GetActor()->GetName()
                                   : TEXT("None"),
               ProbeHit.ImpactNormal.Z, WalkableFloorZ,
               FVector::Dist(StartLocation, ProbeHit.ImpactPoint), *Who);

    if (!bWalkableSurface) {
      // 撞墙 / 撞柱子：原地不动，而不是穿过去
      WFLOG_WARNING("[闪现] 被拒：落点方向被非可站立面阻挡（法线 Z=%.3f < %.3f，"
                    "障碍 %s）。宿主 %s",
                    ProbeHit.ImpactNormal.Z, WalkableFloorZ,
                    ProbeHit.GetActor() ? *ProbeHit.GetActor()->GetName()
                                        : TEXT("None"),
                    *Who);
      return false;
    }

    // 地面挡路：缩短距离，让落点贴着地面而不是嵌进去
    const FVector FlatOffset = DesiredLocation - ProbeHit.ImpactPoint;
    const float WalkableScale =
        FMath::Clamp(DashDirection | FlatOffset, 0.0f, RequestedDistance);
    DesiredLocation = StartLocation + DashDirection * WalkableScale;
  }

  // 用引擎的胶囊体扫描做真正的位置校验（自己写穿透检测容易漏掉斜坡 / 台阶）
  FHitResult MoveHit;
  Owner->SetActorLocation(DesiredLocation, /*bSweep=*/true, &MoveHit,
                          ETeleportType::TeleportPhysics);

  FVector FinalLocation = Owner->GetActorLocation();
  if (MoveHit.bBlockingHit) {
    // 被挡下：沿碰撞法线退一点，避免落点正好卡在几何体表面
    FinalLocation = MoveHit.Location + MoveHit.ImpactNormal * 0.1f;
    if (MoveHit.ImpactNormal.Z >= WalkableFloorZ) {
      // 站在地板上：取整到整数高度，别把胶囊体嵌进地面
      // （UE 5.7 里浮点重载 FMath::RoundToFloat 已移除，用 RoundToDouble）
      FinalLocation.Z = static_cast<float>(FMath::RoundToDouble(FinalLocation.Z));
    }
    Owner->SetActorLocation(FinalLocation, /*bSweep=*/false, nullptr,
                            ETeleportType::TeleportPhysics);
    WFLOG_INFO("[闪现] 位移被碰撞修正：%s（法线 Z=%.3f），落点 %.1f,%.1f,%.1f。"
               "宿主 %s",
               MoveHit.GetActor() ? *MoveHit.GetActor()->GetName() : TEXT("None"),
               MoveHit.ImpactNormal.Z, FinalLocation.X, FinalLocation.Y,
               FinalLocation.Z, *Who);
  }

  // 落点校验：悬空落点（脚下没有可站立面）直接判定失败并退回原位，
  // 否则玩家会「闪」到半空然后掉下来，很难解释也不好用
  FHitResult FloorHit;
  const FVector FloorTraceStart =
      FinalLocation + FVector(0.0, 0.0, CapsuleHalfHeight + 2.0f);
  const FVector FloorTraceEnd =
      FinalLocation - FVector(0.0, 0.0, CapsuleHalfHeight + 2.0f);

  const bool bHasWalkableFloor =
      Movement->IsMovingOnGround() ||
      (World->SweepSingleByChannel(FloorHit, FloorTraceStart, FloorTraceEnd,
                                   FQuat::Identity, TraceChannel, CapsuleShape,
                                   QueryParams) &&
       FloorHit.ImpactNormal.Z >= WalkableFloorZ);

  if (!bHasWalkableFloor) {
    Owner->SetActorLocation(StartLocation, /*bSweep=*/false, nullptr,
                            ETeleportType::TeleportPhysics);
    WFLOG_WARNING("[闪现] 被拒：落点 %.1f,%.1f,%.1f 下方没有可站立面，已回退到 "
                  "%.1f,%.1f,%.1f。宿主 %s",
                  FinalLocation.X, FinalLocation.Y, FinalLocation.Z,
                  StartLocation.X, StartLocation.Y, StartLocation.Z, *Who);
    // 表现层也要知道「这次没闪成」——特效可以放个失败反馈（音效 / UI 闪红）
    Multicast_PlayBlinkEffects(StartLocation, /*bSucceeded=*/false);
    return false;
  }

  // 速度处理：闪现本身是位移，不是冲量。不保留速度就清零，避免落地后被旧速度带着滑
  FVector NewVelocity = Movement->Velocity;
  if (!bKeepVelocity) {
    NewVelocity = FVector::ZeroVector;
  } else {
    // 保留水平速度，去掉竖直分量（否则会把上一帧的下落 / 起跳速度带过来）
    NewVelocity.Z = 0.0;
  }
  const FVector OldVelocity = Movement->Velocity;
  Movement->Velocity = NewVelocity;

  OutLandingLocation = FinalLocation;

  const float ActualDistance = FVector::Dist(StartLocation, FinalLocation);
  WFLOG_INFO("[闪现] 成功：从 %.1f,%.1f,%.1f 闪到 %.1f,%.1f,%.1f"
             "（请求 %.1f，收敛后 %.1f，实际 %.1f，保留速度 %d，"
             "速度 %.1f,%.1f,%.1f -> %.1f,%.1f,%.1f）。宿主 %s",
             StartLocation.X, StartLocation.Y, StartLocation.Z, FinalLocation.X,
             FinalLocation.Y, FinalLocation.Z, Distance, RequestedDistance,
             ActualDistance, bKeepVelocity ? 1 : 0, OldVelocity.X,
             OldVelocity.Y, OldVelocity.Z, NewVelocity.X, NewVelocity.Y,
             NewVelocity.Z, *Who);

  // 表现层：蒙太奇 + 特效，各端各自播一次
  Multicast_PlayBlinkMontage(FinalLocation);
  Multicast_PlayBlinkEffects(FinalLocation, /*bSucceeded=*/true);
  return true;
}

bool UBlinkComponent::BlinkToConfiguredDistance_Implementation(
    FVector &OutLandingLocation) {
  WF_COMPONENT_AUTHORITY_GUARD(false);
  // 距离与上限都用配置值：客户端的 Server_Blink 走的就是这条路径
  return BlinkForward(OutLandingLocation, MaxBlinkDistance, MaxBlinkDistance,
                      bKeepVelocityAfterBlink, BlinkTraceChannel);
}

void UBlinkComponent::ResetBlinkCooldown() {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  LastBlinkTime = -1000.0f;
  WFLOG_INFO("[闪现] 冷却已清除。宿主 %s",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
}

// ===== 表现同步 =====

void UBlinkComponent::Multicast_PlayBlinkMontage_Implementation(
    FVector InLandingLocation) {
  // Multicast 会被复制到所有端，客户端理论上也能反过来调用它（发包合法）。
  // 这里加门禁：只允许服务器发起表现同步，避免客户端拿它刷屏。
  if (!IsAuthoritativeForActorComponent(this)) {
    WFLOG_ERROR("[闪现] Multicast_PlayBlinkMontage 被非权威端调用，已忽略。");
    return;
  }

  if (BlinkMontage == nullptr) {
    // 空配置是**设计内的合法状态**（内容仓库里当前没有闪现蒙太奇），
    // 所以这里用 INFO 而不是 WARNING，避免每次闪现都刷一条告警。
    WFLOG_INFO("[闪现] 未配置 BlinkMontage，跳过闪现蒙太奇播放"
               "（位移已经完成，不受影响）。宿主 %s",
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return;
  }

  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  USkeletalMeshComponent *Mesh = OwnerChar ? OwnerChar->GetMesh() : nullptr;
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    WFLOG_WARNING("[闪现] 播放闪现蒙太奇失败：宿主 %s 没有 AnimInstance。",
                  OwnerChar ? *OwnerChar->GetName() : TEXT("None"));
    return;
  }

  // 这里**不订阅** OnMontageEnded：闪现是一次性位移，
  // 生命周期由 BlinkForward 的返回值决定，不该反过来被动画状态影响。
  const float PlayLength =
      AnimInst->Montage_Play(BlinkMontage,
                             FMath::Max(BlinkMontagePlayRate,
                                        KINDA_SMALL_NUMBER));
  if (PlayLength <= 0.0f) {
    WFLOG_WARNING("[闪现] 闪现蒙太奇 %s 播放失败（返回 %.2f，宿主 %s）。",
                  *BlinkMontage->GetName(), PlayLength,
                  OwnerChar ? *OwnerChar->GetName() : TEXT("None"));
    return;
  }

  WFLOG_INFO("[闪现] 已播放闪现蒙太奇 %s（时长 %.2fs，速率 %.2f），落点 "
             "%.1f,%.1f,%.1f。宿主 %s",
             *BlinkMontage->GetName(), PlayLength, BlinkMontagePlayRate,
             InLandingLocation.X, InLandingLocation.Y, InLandingLocation.Z,
             OwnerChar ? *OwnerChar->GetName() : TEXT("None"));
}

void UBlinkComponent::Multicast_PlayBlinkEffects_Implementation(
    FVector InLandingLocation, bool bSucceeded) {
  if (!IsAuthoritativeForActorComponent(this)) {
    WFLOG_ERROR("[闪现] Multicast_PlayBlinkEffects 被非权威端调用，已忽略。");
    return;
  }

  WFLOG_INFO("[闪现] 广播闪现表现：落点 %.1f,%.1f,%.1f，成功=%d。宿主 %s",
             InLandingLocation.X, InLandingLocation.Y, InLandingLocation.Z,
             bSucceeded ? 1 : 0,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));

  // 每端各播一次（服务器这次是本地执行，不额外发 RPC）
  PlayBlinkEffects(InLandingLocation, bSucceeded);
  // 本地广播：订阅者（镜头 / 残影 / UI）零延迟响应，不必等状态复制
  OnBlinkPerformed.Broadcast(InLandingLocation, bSucceeded);
}

void UBlinkComponent::PlayBlinkEffects_Implementation(
    FVector InLandingLocation, bool bSucceeded) {
  // 本地表现入口：蓝图覆写本事件即可（残影 / 音效 / 镜头抖动 / 失败反馈）。
  // C++ 默认什么都不做。
}

// ===== 客户端 -> 服务器：闪现请求 =====

bool UBlinkComponent::Server_Blink_Validate() { return true; }

void UBlinkComponent::Server_Blink_Implementation() {
  // 服务器（含单机 / 监听服务器）上 Server RPC 就地同步执行，
  // 所以单机与联机走的是同一条路径，不需要写 HasAuthority() 分支。
  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[闪现] RPC Server_Blink 到达服务器（宿主 %s）。%s", *Who,
             *GetBlinkDebugString());

  // 总开关与冷却只在服务器侧判定：客户端能改的只是本地副本，改不动这里的判定
  if (!bCanBlink) {
    WFLOG_WARNING("[闪现] 被拒：bCanBlink=false（总开关关着）。宿主 %s", *Who);
    return;
  }
  if (!IsBlinkReady()) {
    WFLOG_INFO("[闪现] 被拒：冷却中，还剩 %.2fs。宿主 %s",
               GetBlinkCooldownRemaining(), *Who);
    return;
  }

  FVector LandingLocation;
  if (BlinkToConfiguredDistance(LandingLocation)) {
    const UWorld *World = GetWorld();
    LastBlinkTime = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
    ++BlinkCount;
    WFLOG_INFO("[闪现] 闪现完成并进入冷却（%.2fs），累计 %d 次。宿主 %s",
               BlinkCooldown, BlinkCount, *Who);
  }
}

bool UBlinkComponent::Server_BlinkForward_Validate(float Distance,
                                                   float MaxDistance,
                                                   bool bKeepVelocity) {
  // 只做廉价检查；真正的上限收敛在 BlinkForward 内部用 MaxBlinkDistance 完成
  return FMath::IsFinite(Distance) && Distance >= 0.0f &&
         Distance <= 100000.0f && FMath::IsFinite(MaxDistance) &&
         MaxDistance >= 0.0f && MaxDistance <= 100000.0f;
}

void UBlinkComponent::Server_BlinkForward_Implementation(
    float Distance, float MaxDistance, bool bKeepVelocity) {
  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[闪现] RPC Server_BlinkForward 到达服务器（Distance=%.1f, "
             "MaxDistance=%.1f, 保留速度=%d，宿主 %s）。",
             Distance, MaxDistance, bKeepVelocity ? 1 : 0, *Who);

  if (!bCanBlink) {
    WFLOG_WARNING("[闪现] 被拒：bCanBlink=false（总开关关着）。宿主 %s", *Who);
    return;
  }
  if (!IsBlinkReady()) {
    WFLOG_INFO("[闪现] 被拒：冷却中，还剩 %.2fs。宿主 %s",
               GetBlinkCooldownRemaining(), *Who);
    return;
  }

  // 落点只用于日志 / 特效，RPC 不回传
  FVector LandingLocation;
  if (BlinkForward(LandingLocation, Distance, MaxDistance, bKeepVelocity,
                   BlinkTraceChannel)) {
    const UWorld *World = GetWorld();
    LastBlinkTime = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
    ++BlinkCount;
    WFLOG_INFO("[闪现] 闪现完成并进入冷却（%.2fs），累计 %d 次。宿主 %s",
               BlinkCooldown, BlinkCount, *Who);
  }
}
