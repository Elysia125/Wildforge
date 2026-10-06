// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/BaseCharacter.h"

#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Utils/WildforgeLog.h"

// Sets default values
ABaseCharacter::ABaseCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// 角色参与网络复制，挂在上面的组件（背包等）才能复制
	bReplicates = true;

	CharacterAttributes = CreateDefaultSubobject<UCharacterAttributes>(TEXT("CharacterAttributes"));
}

// Called when the game starts or when spawned
void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 记录初始基准速度：之后无论加速多少次，重置都回到设计器里配的那个值
	CaptureBaseMaxSpeed();
}

// Called every frame
void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}

// ===== 速度提升（长按加速） =====
// 设计要点：
//   1. 真正的权威状态只有「CharacterMovement 的 MaxWalkSpeed」一个，它由
//      UCharacterMovementComponent 自带复制（OnRep_MaxWalkSpeed）——不需要我们再复制一份，
//      客户端本地移动与服务器校验用的是同一个值。
//   2. 因此本能力全部在权威端跑：客户端自己算一套速度只会和服务器打架（客户端改自己的
//      MaxWalkSpeed 既留不住、又会让服务器回滚），所以客户端只能发 Server_* 请求。
//   3. 基准速度（BaseMaxWalkSpeed）只在「未加速」时抓取，保证不会被加速后的值污染。

void ABaseCharacter::CaptureBaseMaxSpeed() {
  const UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return;
  }

  SprintBoost.BaseMaxWalkSpeed = Movement->MaxWalkSpeed;
  SprintBoost.BaseMaxWalkSpeedCrouched = Movement->MaxWalkSpeedCrouched;

  // 蹲伏/步行的基准比例（UE 默认 600/300 = 0.5）。加速时按它同步缩放蹲伏速度，
  // 基准步行速度为 0 时退回 1.0（避免除零，也不会把蹲伏速度压成 0）。
  SprintBoost.CrouchSpeedRatio =
      (Movement->MaxWalkSpeed > KINDA_SMALL_NUMBER)
          ? (Movement->MaxWalkSpeedCrouched / Movement->MaxWalkSpeed)
          : 1.0f;
  SprintBoost.CrouchSpeedRatio = FMath::Max(SprintBoost.CrouchSpeedRatio, 0.0f);
}

void ABaseCharacter::ApplyMaxWalkSpeed(float InMaxWalkSpeed, float Alpha) {
  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return;
  }

  Movement->MaxWalkSpeed = InMaxWalkSpeed;
  // 蹲伏上限按基准比例跟着抬（600/300 的配置加速到 900 时蹲伏是 450），
  // 而不是直接塞成和步行一样快——那样加速中蹲下就没有任何代价了。
  Movement->MaxWalkSpeedCrouched = InMaxWalkSpeed * SprintBoost.CrouchSpeedRatio;

  OnSpeedBoostUpdated(Movement->MaxWalkSpeed, Alpha);
}

void ABaseCharacter::ClearSprintBoostTimer() {
  if (const UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(SprintBoost.TimerHandle);
  }
  SprintBoost.TimerHandle.Invalidate();
  SprintBoost.bIntervalTimer = false;
}

void ABaseCharacter::TickSprintBoost() {
  // ApplyMaxWalkSpeed 会广播 OnSpeedBoostUpdated，蓝图有机会在里面 Reset 或重开加速，
  // 所以先记下本次回调属于哪一代，续挂定时器之前用它确认状态没被改过。
  const uint32 MyGeneration = SprintBoost.Generation;

  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr || !SprintBoost.bBoostActive) {
    // 移动组件没了，或加速已被 ResetMaxSpeed 结束：定时器没有继续存在的意义
    ClearSprintBoostTimer();
    return;
  }

  // 用世界时间差累计，而不是 GetTimerElapsed(Handle)：后者是按 Rate 反推出来的
  // （TimerManager.cpp:795-812），掉帧补触发（CallCount > 1）时会算出负值，ElapsedTime 会倒退。
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  const float DeltaSeconds = FMath::Max(Now - SprintBoost.LastUpdateTime, 0.0f);
  SprintBoost.LastUpdateTime = Now;
  SprintBoost.ElapsedTime += DeltaSeconds;

  const float Duration = FMath::Max(SprintBoost.BoostDuration, KINDA_SMALL_NUMBER);
  const float Alpha = FMath::Clamp(SprintBoost.ElapsedTime / Duration, 0.0f, 1.0f);

  // 线性插值：起始速度 -> 目标速度。想改成缓入缓出就换成 FMath::InterpEaseInOut
  const float NewMaxWalkSpeed =
      FMath::Lerp(SprintBoost.StartSpeed, SprintBoost.TargetSpeed, Alpha);

  if (Alpha >= 1.0f) {
    // 到顶：停表（不再每帧空转），速度保持到 ResetMaxSpeed 为止。
    // 注意这里只清定时器，bBoostActive 要留着，否则 IsSpeedBoostActive() 会提前变 false。
    ApplyMaxWalkSpeed(SprintBoost.TargetSpeed, 1.0f);
    ClearSprintBoostTimer();
    return;
  }

  ApplyMaxWalkSpeed(NewMaxWalkSpeed, Alpha);

  // 这张蓝图广播之后状态可能已经被改过（重置 / 重开一代），确认仍然有效才续挂，
  // 否则会出现两条定时器链同时跑（旧链的句柄被覆盖，谁也停不掉它）。
  if (SprintBoost.bBoostActive && !SprintBoost.bIntervalTimer &&
      SprintBoost.Generation == MyGeneration) {
    // 每帧模式：重新挂一次 next-tick 定时器，下一次引擎 tick 再进来。
    // 绝不能改成 SetTimer(..., 0.f, true)——InRate <= 0 在引擎里是「清掉定时器」的意思，
    // 结果是一次都不会触发（见 bug-017）。
    SprintBoost.TimerHandle = GetWorldTimerManager().SetTimerForNextTick(
        this, &ABaseCharacter::TickSprintBoost);
  }
}

bool ABaseCharacter::StartSpeedBoost(float HoldThreshold, float TargetMaxSpeed,
                                     float BoostDuration, float BoostInterval) {
  if (!HasAuthority()) {
    WFLOG_ERROR("ABaseCharacter::StartSpeedBoost 在非权威端被调用，已忽略；"
                "客户端请改用 Server_StartSpeedBoost。");
    return false;
  }

  // HoldThreshold 是调用方的节奏参数（先按住这么久才算「长按」），
  // 不参与速度曲线计算，这里只做记录与合法性检查。
  if (!FMath::IsFinite(HoldThreshold) || HoldThreshold < 0.0f) {
    WFLOG_WARNING("StartSpeedBoost: 长按阈值 %.3f 非法，已忽略。", HoldThreshold);
    return false;
  }
  // 目标速度必须真的比基准速度高，否则「加速」只会把角色改慢
  if (!FMath::IsFinite(TargetMaxSpeed) ||
      TargetMaxSpeed <= SprintBoost.BaseMaxWalkSpeed) {
    WFLOG_WARNING("StartSpeedBoost: 目标速度 %.2f 不大于基准速度 %.2f，已忽略。",
                  TargetMaxSpeed, SprintBoost.BaseMaxWalkSpeed);
    return false;
  }
  if (!FMath::IsFinite(BoostDuration) || BoostDuration <= 0.0f) {
    WFLOG_WARNING("StartSpeedBoost: 加速时长 %.3f 非法，已忽略。", BoostDuration);
    return false;
  }
  if (!FMath::IsFinite(BoostInterval) || BoostInterval < 0.0f) {
    WFLOG_WARNING("StartSpeedBoost: 刷新间隔 %.3f 非法，已忽略。", BoostInterval);
    return false;
  }

  if (IsSpeedBoostActive()) {
    // 加速中重复触发（例如蓝图每帧调一次）：保留已经在跑的这一次
    WFLOG_WARNING("StartSpeedBoost: 已在加速中（目标 %.2f，进度 %.2f），本次调用被忽略。",
                  SprintBoost.TargetSpeed, GetSpeedBoostAlpha());
    return false;
  }

  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return false;
  }

  // 先还原基准速度，再记起始速度：避免上一轮加速后的值被当成基准值
  ResetMaxSpeed();
  CaptureBaseMaxSpeed();

  SprintBoost.StartSpeed = Movement->MaxWalkSpeed;
  SprintBoost.TargetSpeed = TargetMaxSpeed;
  SprintBoost.BoostDuration = BoostDuration;
  SprintBoost.ElapsedTime = 0.0f;
  SprintBoost.bBoostActive = true;
  // 代次自增：上一轮遗留的 next-tick 回调即使还在路上，也不会再续挂定时器
  ++SprintBoost.Generation;

  const UWorld *World = GetWorld();
  SprintBoost.LastUpdateTime = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;

  // 立刻推一次起始速度，让 UI/特效不必等下一帧（ElapsedTime 还是 0，所以就是起始速度）
  ApplyMaxWalkSpeed(SprintBoost.StartSpeed, 0.0f);

  if (BoostInterval > 0.0f) {
    // 固定间隔：普通循环定时器
    SprintBoost.bIntervalTimer = true;
    GetWorldTimerManager().SetTimer(SprintBoost.TimerHandle, this,
                                    &ABaseCharacter::TickSprintBoost,
                                    BoostInterval, true);
  } else {
    // 每帧：必须用 next-tick 定时器链，后续每帧由 TickSprintBoost 自己续挂。
    // 不能写 SetTimer(..., 0.f, /*bLoop=*/true)：引擎对 InRate <= 0 的处理是「清掉该句柄上的
    // 定时器」（TimerManager.h:157 的注释、TimerManager.cpp:617-659 的实现），
    // 传 0 的结果是定时器压根不存在，一次都不会触发（见 bug-017）。
    SprintBoost.bIntervalTimer = false;
    SprintBoost.TimerHandle = GetWorldTimerManager().SetTimerForNextTick(
        this, &ABaseCharacter::TickSprintBoost);
  }

  WFLOG_INFO(
      "StartSpeedBoost: 长按阈值 %.2fs，速度 %.2f -> %.2f（基准 %.2f / 蹲伏 %.2f，比例 %.2f），"
      "用时 %.2fs，刷新间隔 %.3fs（0 表示每帧）。",
      HoldThreshold, SprintBoost.StartSpeed, SprintBoost.TargetSpeed,
      SprintBoost.BaseMaxWalkSpeed, SprintBoost.BaseMaxWalkSpeedCrouched,
      SprintBoost.CrouchSpeedRatio, BoostDuration, BoostInterval);
  return true;
}

void ABaseCharacter::ResetMaxSpeed() {
  if (!HasAuthority()) {
    WFLOG_ERROR("ABaseCharacter::ResetMaxSpeed 在非权威端被调用，已忽略；"
                "客户端请改用 Server_StopSpeedBoost。");
    return;
  }

  // 未加速时调用是安全的空操作（Server_StopSpeedBoost 可以被重复发）
  ClearSprintBoostTimer();
  SprintBoost.bBoostActive = false;

  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return;
  }

  // 两个基准值都精确还原（不走 ApplyMaxWalkSpeed 的比例乘法，避免浮点误差累积）
  Movement->MaxWalkSpeed = SprintBoost.BaseMaxWalkSpeed;
  Movement->MaxWalkSpeedCrouched = SprintBoost.BaseMaxWalkSpeedCrouched;

  SprintBoost.StartSpeed = SprintBoost.BaseMaxWalkSpeed;
  SprintBoost.TargetSpeed = SprintBoost.BaseMaxWalkSpeed;
  SprintBoost.ElapsedTime = 0.0f;

  OnSpeedBoostUpdated(Movement->MaxWalkSpeed, 0.0f);
}

void ABaseCharacter::SetBaseMaxSpeed(float InBaseMaxWalkSpeed,
                                     float InBaseMaxWalkSpeedCrouched) {
  if (!HasAuthority()) {
    WFLOG_ERROR("ABaseCharacter::SetBaseMaxSpeed 在非权威端被调用，已忽略。");
    return;
  }

  if (!FMath::IsFinite(InBaseMaxWalkSpeed) || InBaseMaxWalkSpeed <= 0.0f) {
    WFLOG_WARNING("SetBaseMaxSpeed: 基准速度 %.2f 非法，已忽略。",
                  InBaseMaxWalkSpeed);
    return;
  }
  if (InBaseMaxWalkSpeedCrouched >= 0.0f &&
      !FMath::IsFinite(InBaseMaxWalkSpeedCrouched)) {
    WFLOG_WARNING("SetBaseMaxSpeed: 蹲伏基准速度 %.2f 非法，已忽略。",
                  InBaseMaxWalkSpeedCrouched);
    return;
  }

  if (IsSpeedBoostActive()) {
    // 加速中改写基准值会污染 ResetMaxSpeed 的还原目标，先拒绝
    WFLOG_WARNING("SetBaseMaxSpeed: 加速进行中，拒绝改写基准速度（当前基准 %.2f）。",
                  SprintBoost.BaseMaxWalkSpeed);
    return;
  }

  SprintBoost.BaseMaxWalkSpeed = InBaseMaxWalkSpeed;
  if (InBaseMaxWalkSpeedCrouched >= 0.0f) {
    // 显式给了蹲伏基准：顺便更新比例，之后的加速都按新比例缩放蹲伏速度
    SprintBoost.BaseMaxWalkSpeedCrouched = InBaseMaxWalkSpeedCrouched;
    SprintBoost.CrouchSpeedRatio = InBaseMaxWalkSpeedCrouched / InBaseMaxWalkSpeed;
  } else {
    // 只改步行基准：蹲伏基准按原有比例推算，保持「蹲伏比步行慢」的相对关系
    SprintBoost.BaseMaxWalkSpeedCrouched =
        InBaseMaxWalkSpeed * SprintBoost.CrouchSpeedRatio;
  }
  ApplyMaxWalkSpeed(InBaseMaxWalkSpeed, 0.0f);
}

bool ABaseCharacter::IsSpeedBoostActive() const {
  // 只看状态标志，不看定时器：到顶后定时器已经清掉，但速度仍然保持在目标值上；
  // 而且定时器回调执行期间句柄状态是 Executing，用 IsTimerActive 判断并不可靠。
  return SprintBoost.bBoostActive;
}

float ABaseCharacter::GetSpeedBoostAlpha() const {
  if (!SprintBoost.bBoostActive) {
    return 0.0f;
  }

  const float Duration = FMath::Max(SprintBoost.BoostDuration, KINDA_SMALL_NUMBER);
  return FMath::Clamp(SprintBoost.ElapsedTime / Duration, 0.0f, 1.0f);
}

// ===== 点按闪现 =====

bool ABaseCharacter::BlinkForward(FVector &OutLandingLocation, float Distance,
                                  float MaxDistance, bool bKeepVelocity,
                                  TEnumAsByte<ECollisionChannel> TraceChannel) {
  OutLandingLocation = GetActorLocation();

  if (!HasAuthority()) {
    WFLOG_ERROR("ABaseCharacter::BlinkForward 在非权威端被调用，已忽略；"
                "客户端请改用 Server_Blink / Server_BlinkForward。");
    return false;
  }

  if (!FMath::IsFinite(Distance) || Distance <= 0.0f || !FMath::IsFinite(MaxDistance)) {
    WFLOG_WARNING("BlinkForward: 距离参数非法（Distance=%.2f, MaxDistance=%.2f），已忽略。",
                  Distance, MaxDistance);
    return false;
  }

  // 服务器收敛：客户端可以要更远，但最多只给 DefaultMaxBlinkDistance / MaxDistance
  const float ServerCap = FMath::Min(MaxDistance, DefaultMaxBlinkDistance);
  const float RequestedDistance = FMath::Min(Distance, FMath::Max(ServerCap, 0.0f));
  if (RequestedDistance <= 0.0f) {
    WFLOG_WARNING("BlinkForward: 收敛后的距离为 0（Distance=%.2f, MaxDistance=%.2f, 服务器上限=%.2f）。",
                  Distance, MaxDistance, DefaultMaxBlinkDistance);
    return false;
  }

  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return false;
  }

  const UCapsuleComponent *Capsule = GetCapsuleComponent();
  if (Capsule == nullptr) {
    return false;
  }

  // 方向取视线水平分量：忽略俯仰角，朝上不会顶天花板、朝下不会钻地
  FVector ViewLocation;
  FRotator ViewRotation;
  GetActorEyesViewPoint(ViewLocation, ViewRotation);

  FVector DashDirection = ViewRotation.Vector();
  DashDirection.Z = 0.0;
  if (!DashDirection.Normalize()) {
    // 视线完全竖直（或没有 Controller）时退回角色自身前向
    DashDirection = GetActorForwardVector();
    DashDirection.Z = 0.0;
    if (!DashDirection.Normalize()) {
      WFLOG_WARNING("BlinkForward: 视线与角色前向都无法给出水平方向，已取消。");
      return false;
    }
  }

  // 胶囊体半高在半蹲时会变小，查询形状要跟着走，否则会误判「钻得过去」
  const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
  const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
  const FCollisionShape CapsuleShape =
      FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);

  // 忽略自身，避免扫描一开始就撞到自己的胶囊体
  FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(BlinkForward), false, this);

  const FVector StartLocation = GetActorLocation();
  FVector DesiredLocation = StartLocation + DashDirection * RequestedDistance;

  // 先探一眼整段位移有没有障碍；有的话按障碍法线把落点贴回可站立面，再重扫一次
  FHitResult ProbeHit;
  if (GetWorld()->SweepSingleByChannel(ProbeHit, StartLocation, DesiredLocation,
                                       FQuat::Identity, TraceChannel,
                                       CapsuleShape, QueryParams)) {
    const float WalkableFloorZ = Movement->GetWalkableFloorZ();
    const bool bWalkableSurface = ProbeHit.ImpactNormal.Z >= WalkableFloorZ;

    if (!bWalkableSurface) {
      // 撞墙/撞柱子：原地不动，而不是穿过去
      WFLOG_WARNING("BlinkForward: 落点方向被非可站立面（法线 Z=%.3f）阻挡，闪现取消。",
                    ProbeHit.ImpactNormal.Z);
      return false;
    }

    // 地面挡路：缩短距离，让落点贴着地面而不是嵌进去
    const FVector FlatOffset = DesiredLocation - ProbeHit.ImpactPoint;
    const float WalkableScale =
        FMath::Clamp(DashDirection | FlatOffset, 0.0f, RequestedDistance);
    DesiredLocation = StartLocation + DashDirection * WalkableScale;
  }

  // 用引擎的胶囊体扫描做真正的位置校验（自己写穿透检测容易漏掉斜坡/台阶）
  FHitResult MoveHit;
  SetActorLocation(DesiredLocation, /*bSweep=*/true, &MoveHit,
                   ETeleportType::TeleportPhysics);

  FVector FinalLocation = GetActorLocation();
  if (MoveHit.bBlockingHit) {
    // 被挡下：沿碰撞法线退一点，避免落点正好卡在几何体表面
    FinalLocation = MoveHit.Location + MoveHit.ImpactNormal * 0.1f;
    if (MoveHit.ImpactNormal.Z >= Movement->GetWalkableFloorZ()) {
      // 站在地板上：取整到整数高度，别把胶囊体嵌进地面
      // （UE 5.7 里浮点重载 FMath::RoundToFloat 已移除，用 RoundToDouble）
      FinalLocation.Z = static_cast<float>(FMath::RoundToDouble(FinalLocation.Z));
    }
    SetActorLocation(FinalLocation, /*bSweep=*/false, nullptr,
                     ETeleportType::TeleportPhysics);
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
      (GetWorld()->SweepSingleByChannel(FloorHit, FloorTraceStart, FloorTraceEnd,
                                        FQuat::Identity, TraceChannel,
                                        CapsuleShape, QueryParams) &&
       FloorHit.ImpactNormal.Z >= Movement->GetWalkableFloorZ());

  if (!bHasWalkableFloor) {
    SetActorLocation(StartLocation, /*bSweep=*/false, nullptr,
                     ETeleportType::TeleportPhysics);
    WFLOG_WARNING("BlinkForward: 落点 %.1f,%.1f,%.1f 下方没有可站立面，闪现已回退。",
                  FinalLocation.X, FinalLocation.Y, FinalLocation.Z);
    return false;
  }

  // 速度处理：闪现本身是位移，不是冲量。不保留速度就清零，避免落地后被旧速度带着滑
  FVector NewVelocity = Movement->Velocity;
  if (!bKeepVelocity) {
    NewVelocity = FVector::ZeroVector;
  } else {
    // 保留水平速度，去掉竖直分量（否则会把上一帧的下落/起跳速度带过来）
    NewVelocity.Z = 0.0;
  }
  Movement->Velocity = NewVelocity;

  OutLandingLocation = FinalLocation;
  WFLOG_INFO("BlinkForward: 从 %.1f,%.1f,%.1f 闪到 %.1f,%.1f,%.1f（请求 %.1f，实际 %.1f，保留速度 %d）。",
             StartLocation.X, StartLocation.Y, StartLocation.Z, FinalLocation.X,
             FinalLocation.Y, FinalLocation.Z, RequestedDistance,
             FVector::Dist(StartLocation, FinalLocation), bKeepVelocity ? 1 : 0);
  return true;
}

// ===== 客户端 -> 服务器：RPC 实现 =====
// _Implementation 里直接调权威函数：服务器/单机宿主上 Server RPC 会就地同步执行，
// 所以单机与联机走同一条路径，不要写 HasAuthority() ? 本地调用 : 发 RPC 的分支。

bool ABaseCharacter::Server_StartSpeedBoost_Validate(float HoldThreshold,
                                                     float TargetMaxSpeed,
                                                     float BoostDuration,
                                                     float BoostInterval) {
  // 只做廉价检查；「目标速度是否高于基准速度」这类需要本地状态的判断在权威函数里做
  return FMath::IsFinite(HoldThreshold) && HoldThreshold >= 0.0f &&
         FMath::IsFinite(TargetMaxSpeed) && TargetMaxSpeed > 0.0f &&
         TargetMaxSpeed <= 10000.0f && FMath::IsFinite(BoostDuration) &&
         BoostDuration > 0.0f && BoostDuration <= 60.0f &&
         FMath::IsFinite(BoostInterval) && BoostInterval >= 0.0f &&
         BoostInterval <= 1.0f;
}

void ABaseCharacter::Server_StartSpeedBoost_Implementation(
    float HoldThreshold, float TargetMaxSpeed, float BoostDuration,
    float BoostInterval) {
  StartSpeedBoost(HoldThreshold, TargetMaxSpeed, BoostDuration, BoostInterval);
}

bool ABaseCharacter::Server_StopSpeedBoost_Validate() { return true; }

void ABaseCharacter::Server_StopSpeedBoost_Implementation() {
  ResetMaxSpeed();
}

bool ABaseCharacter::Server_BlinkForward_Validate(float Distance,
                                                  float MaxDistance,
                                                  bool bKeepVelocity) {
  // 只做廉价检查；真正的上限收敛在 BlinkForward 内部用 DefaultMaxBlinkDistance 完成
  return FMath::IsFinite(Distance) && Distance >= 0.0f && Distance <= 100000.0f &&
         FMath::IsFinite(MaxDistance) && MaxDistance >= 0.0f &&
         MaxDistance <= 100000.0f;
}

void ABaseCharacter::Server_BlinkForward_Implementation(float Distance,
                                                        float MaxDistance,
                                                        bool bKeepVelocity) {
  // 落点只用于日志/特效，RPC 不回传
  FVector LandingLocation;
  BlinkForward(LandingLocation, Distance, MaxDistance, bKeepVelocity,
               ECC_Visibility);
}

bool ABaseCharacter::Server_Blink_Validate() { return true; }

void ABaseCharacter::Server_Blink_Implementation() {
  FVector LandingLocation;
  BlinkForward(LandingLocation, DefaultMaxBlinkDistance,
               DefaultMaxBlinkDistance, false, ECC_Visibility);
}
