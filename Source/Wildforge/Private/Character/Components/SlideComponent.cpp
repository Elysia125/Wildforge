// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/SlideComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/Settings/SlideComponentSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Utils/ComponentAuthorityGuard.h"
#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"

// 滑行的生命周期（读懂这段再改）：
//
//   客户端按键 → Server_StartSlide（RPC）
//     → 服务器 StartSlide()：判定通过后写 bIsSliding / LastSlideTime（权威玩法状态）
//     → Multicast_BeginSlide(段名)：在**每个端**本地执行一次
//          · 各端自己 Montage_Play（滑行动画能被别人看到，靠的就是这一步：
//            引擎默认不复制蒙太奇播放）
//          · 各端广播 OnSlideStarted —— 订阅者（APlayerCharacter）据此落**软锁**
//          · **权威端 + 本地控制的自主代理**写下滑行用的移动参数（零摩擦 / 小加速度）
//            并给一个起手冲量（本地预测镜像，见头文件「为什么自主代理也要本地镜像」）
//     → 服务器挂兜底定时器（防 Tick 没跑）
//     → 服务器每帧 TickAuthoritySlide()：总开关 / 离地 / 时长到期 / 速度过低 → FinishSlide()
//          · FinishSlide 复位 bIsSliding + Multicast_EndSlide()
//          · Multicast_EndSlide 在每个端：停动画 + 还原移动参数 + 广播 OnSlideFinished
//
// 关键：Started / Finished 的判据是**本端的表现标志** bSlidePresentationActive，
// 而不是复制的 bIsSliding —— 后者是 COND_OwnerOnly，模拟代理（其他玩家）永远收不到，
// 拿它当判据会让那些端「只落锁、没人解锁」（见 bug-030）。
//
// 位移：权威端每帧把水平速度夹到速度曲线上（只往下夹）。摩擦 / 刹车减速度置 0 让引擎
// 不要插手减速（`ApplyVelocityBraking` 在两者都为 0 时直接 return，
// CharacterMovementComponent.cpp:4310-4316）；MaxAcceleration 只用来给一点点转向权限。

namespace {
// 兜底定时器在滑行时长之外多等的余量（秒）：给 BlendOut / 掉帧留一点空间
constexpr float SlideTimeoutPaddingSeconds = 1.0f;
} // namespace

USlideComponent::USlideComponent() {
  // 滑行必须逐帧推进位移，所以 Tick 常开；不在滑行时靠 TickComponent 开头早退
  // （为什么不动态开关 Tick，见头文件 TickComponent 的说明）。
  PrimaryComponentTick.bCanEverTick = true;

  // 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）。
  // 缺了这一行 DOREPLIFETIME_* 等于没写——bIsSliding / 冷却到不了客户端，
  // 客户端就没法做 UI 与输入门控。
  SetIsReplicatedByDefault(true);
}

void USlideComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 这几个都是「服务器写、客户端只读」的状态。
  //
  // ⚠️ 用 COND_OwnerOnly 而不是 COND_SimulatedOnly：后者**不会**发给自主代理
  // （AutonomousProxy，也就是本地玩家自己），恰好是最需要这些值的那个端
  // （本项目在 LandRoll 上踩过，见 bug-030）。想让**所有人**都读到，就得去掉 condition。
  DOREPLIFETIME_CONDITION(USlideComponent, bIsSliding, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, LastSlideTime, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideCooldown, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideCount, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, ActiveSlideMontage, COND_OwnerOnly);

  // 调参（类默认值 = 基线，可被 USlideComponentSettings 的 ini 覆盖）。
  //
  // `COND_InitialOnly` = 只在出生束里发一次：这些值在角色生命周期内不会变，
  // 发一次足够、之后零开销；出生束在客户端 BeginPlay 之前落地，所以本端的
  // 「滑行专用移动参数 + 速度曲线」从第一帧起就是用服务器那份值在跑。
  //
  // ⚠️ 这一节**必须**复制，不能省：滑行是两端各自跑同一条曲线、各自写同一套移动参数
  //    的能力（见类注释「为什么自主代理也要本地镜像」）。ini 一改，客户端若还按自己的
  //    类默认值预测，就是 bug-029 那种「每秒被 ClientAdjustPosition 拽回一次」的橡皮筋。
  //
  // 注意 `SlideCooldown` 不在下面：它原本就有复制通道（上面的 COND_OwnerOnly），
  // ini 覆盖它时走的还是那条。
  DOREPLIFETIME_CONDITION(USlideComponent, SlideDuration, COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideStartSpeed, COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideEndSpeed, COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideMaxSpeedDecelRate,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideMinSpeedToContinue,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideMinSpeedGraceTime,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, MinSpeedToStartSlide,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, bAllowSteering, COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideMaxAcceleration,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideGroundFriction,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, SlideBrakingDeceleration,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, bEndSlideWhenAirborne,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, MontagePlayRate, COND_InitialOnly);
  DOREPLIFETIME_CONDITION(USlideComponent, MontageStopBlendOutTime,
                          COND_InitialOnly);
}

void USlideComponent::BeginPlay() {
  Super::BeginPlay();

  // ini 覆盖要先应用（权威端）：下面的「组件就绪」日志按最终生效值打印。
  ApplyGameplaySettingsOverrides();

  const AActor *Owner = GetOwner();
  const FString Who = Owner ? Owner->GetName() : TEXT("None");

  if (Cast<ACharacter>(Owner) == nullptr) {
    WFLOG_WARNING("[滑行] BeginPlay：宿主 %s 不是 ACharacter，滑行只能有状态、没有表现。",
                  *Who);
  }
  if (GetOwnerMovementComponent() == nullptr) {
    WFLOG_WARNING("[滑行] BeginPlay：宿主 %s 没有 UCharacterMovementComponent，"
                  "滑行会被 StartSlide 直接拒绝（没有位移载体）。",
                  *Who);
  }

  WFLOG_INFO("[滑行] 组件就绪：宿主 %s，本端权威=%d，本地控制=%d，长度 %.2fs，"
             "起始速度下限 %.0f，结束速度 %.0f，最低速度 %.0f（宽限 %.2fs），"
             "起滑速度要求 %.0f，冷却 %.2fs，转向=%d（最大加速度 %.0f），"
             "滑行摩擦 %.2f / 刹车减速 %.2f，离地结束=%d，本地镜像=%d，"
             "蒙太奇=%s（播速 %.2f，段名=%s，淡出 %.2fs），总开关=%d。",
             *Who, IsAuthoritativeForActorComponent(this) ? 1 : 0,
             IsLocallyControlledOwner() ? 1 : 0, SlideDuration, SlideStartSpeed,
             SlideEndSpeed, SlideMinSpeedToContinue, SlideMinSpeedGraceTime,
             MinSpeedToStartSlide, SlideCooldown, bAllowSteering ? 1 : 0,
             SlideMaxAcceleration, SlideGroundFriction, SlideBrakingDeceleration,
             bEndSlideWhenAirborne ? 1 : 0, bMirrorSlideOnOwningClient ? 1 : 0,
             SlideMontage ? *SlideMontage->GetName() : TEXT("None"),
             MontagePlayRate,
             SlideSectionName.IsNone() ? TEXT("None")
                                       : *SlideSectionName.ToString(),
             MontageStopBlendOutTime, bCanSlide ? 1 : 0);
}

void USlideComponent::ApplyGameplaySettingsOverrides() {
  // 只有权威端读 ini（客户端拿复制下来的值，见 SlideComponentSettings.h 的类注释）。
  // 组件里不能用 HasAuthority()（AActor 的方法，写了直接 C3861）。
  if (!IsAuthoritativeForActorComponent(this)) {
    return;
  }

  const USlideComponentSettings *Settings =
      USlideComponentSettings::Get();
  if (Settings == nullptr) {
    WFLOG_ERROR("[配置] 滑行组件取不到 USlideComponentSettings（CDO 为空），"
                "本次不应用任何 ini 覆盖。宿主 %s",
                GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return;
  }

  // 夹紧的下限与组件 meta 的 ClampMin 一致：ini 是手写文本，不保证不越界
  // （`SlideDuration` 还是曲线进度公式的除数，填 0 会算出无穷大的进度）。
  int32 Applied = 0;
  if (Settings->bOverride_Duration) {
    SlideDuration = FMath::Max(0.01f, Settings->Duration);
    ++Applied;
  }
  if (Settings->bOverride_StartSpeed) {
    SlideStartSpeed = FMath::Max(0.0f, Settings->StartSpeed);
    ++Applied;
  }
  if (Settings->bOverride_EndSpeed) {
    SlideEndSpeed = FMath::Max(0.0f, Settings->EndSpeed);
    ++Applied;
  }
  if (Settings->bOverride_MaxSpeedDecelRate) {
    SlideMaxSpeedDecelRate = FMath::Max(0.0f, Settings->MaxSpeedDecelRate);
    ++Applied;
  }
  if (Settings->bOverride_MinSpeedToContinue) {
    SlideMinSpeedToContinue =
        FMath::Max(0.0f, Settings->MinSpeedToContinue);
    ++Applied;
  }
  if (Settings->bOverride_MinSpeedGraceTime) {
    SlideMinSpeedGraceTime = FMath::Max(0.0f, Settings->MinSpeedGraceTime);
    ++Applied;
  }
  if (Settings->bOverride_MinSpeedToStartSlide) {
    MinSpeedToStartSlide = FMath::Max(0.0f, Settings->MinSpeedToStartSlide);
    ++Applied;
  }
  if (Settings->bOverride_AllowSteering) {
    bAllowSteering = Settings->bAllowSteering;
    ++Applied;
  }
  if (Settings->bOverride_MaxAcceleration) {
    SlideMaxAcceleration = FMath::Max(0.0f, Settings->MaxAcceleration);
    ++Applied;
  }
  if (Settings->bOverride_GroundFriction) {
    SlideGroundFriction = FMath::Max(0.0f, Settings->GroundFriction);
    ++Applied;
  }
  if (Settings->bOverride_BrakingDeceleration) {
    SlideBrakingDeceleration =
        FMath::Max(0.0f, Settings->BrakingDeceleration);
    ++Applied;
  }
  if (Settings->bOverride_EndSlideWhenAirborne) {
    bEndSlideWhenAirborne = Settings->bEndSlideWhenAirborne;
    ++Applied;
  }
  if (Settings->bOverride_MontagePlayRate) {
    MontagePlayRate = FMath::Max(0.01f, Settings->MontagePlayRate);
    ++Applied;
  }
  if (Settings->bOverride_MontageStopBlendOutTime) {
    MontageStopBlendOutTime = FMath::Max(0.0f, Settings->MontageStopBlendOutTime);
    ++Applied;
  }

  const FString Source = (Applied > 0)
                             ? FString::Printf(TEXT("应用了 %d 项 ini 覆盖"), Applied)
                             : FString(TEXT("没有 ini 覆盖（全部用类默认值）"));
  WFLOG_INFO("[配置] 滑行组件（宿主 %s，权威端）：%s；生效值 时长=%.2fs "
             "起速=%.0f 末速=%.0f 最低速度=%.0f(宽限 %.2fs) 起滑要求=%.0f "
             "转向=%d(加速度 %.0f) 摩擦=%.2f 刹车=%.2f 离地结束=%d "
             "蒙太奇播速=%.2f 淡出=%.2fs。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"), *Source,
             SlideDuration, SlideStartSpeed, SlideEndSpeed,
             SlideMinSpeedToContinue, SlideMinSpeedGraceTime,
             MinSpeedToStartSlide, bAllowSteering ? 1 : 0, SlideMaxAcceleration,
             SlideGroundFriction, SlideBrakingDeceleration,
             bEndSlideWhenAirborne ? 1 : 0, MontagePlayRate,
             MontageStopBlendOutTime);
}

void USlideComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // 清兜底定时器：回调绑在 this 上，组件销毁后触发会打到半销毁对象
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(SlideTimeoutHandle);
  }

  // 把本端被改写的移动参数还回去。少了这一步，「滑行中途组件被移除 / 角色被销毁」
  // 会让 GroundFriction / MaxAcceleration 永久停在滑行值上（角色一直在溜冰）。
  RestoreSlideMovementOverrides(TEXT("组件销毁（EndPlay）"));

  if (bSlidePresentationActive) {
    WFLOG_INFO("[滑行] EndPlay：本端仍在滑行表现中（原因=%d），已就地收尾。%s",
               static_cast<int32>(EndPlayReason), *GetSlideDebugString());
    bSlidePresentationActive = false;
  }

  Super::EndPlay(EndPlayReason);
}

// ===== 本端辅助 =====

UCharacterMovementComponent *
USlideComponent::GetOwnerMovementComponent() const {
  const AActor *Owner = GetOwner();
  return Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
               : nullptr;
}

bool USlideComponent::IsLocallyControlledOwner() const {
  const APawn *Pawn = Cast<APawn>(GetOwner());
  return Pawn != nullptr && Pawn->IsLocallyControlled();
}

bool USlideComponent::ShouldDriveSlideLocally() const {
  // 权威端：它就是玩法的真相源，位移必须由它推进
  if (IsAuthoritativeForActorComponent(this)) {
    return true;
  }
  // 本地控制的自主代理：客户端自己会做移动预测，参数与曲线必须和服务器一致，
  // 否则预测出来的位置会被服务器反复纠正（橡皮筋，见 bug-029）。
  return bMirrorSlideOnOwningClient && IsLocallyControlledOwner();
}

FVector USlideComponent::ResolveSlideDirection() const {
  const UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement != nullptr) {
    // ① 有水平速度就顺着速度方向滑出去（冲刺 / 跑动中起滑的常见情况，动量优先）
    const FVector HorizontalVelocity(Movement->Velocity.X, Movement->Velocity.Y,
                                     0.0f);
    const FVector VelocityDirection = HorizontalVelocity.GetSafeNormal();
    if (!VelocityDirection.IsNearlyZero()) {
      return VelocityDirection;
    }

    // ② 没有速度（站着起滑）：用**输入加速度**的方向。
    // ⚠️ 不能用 GetLastInputVector()：它只由客户端 AddMovementInput 累加，
    // 服务器上恒为零向量（bug-026）。Acceleration 是 move 包里带过来的，
    // 两端都有效（CharacterMovementComponent.cpp:10512）。
    const FVector Acceleration = Movement->GetCurrentAcceleration();
    const FVector AccelerationDirection =
        FVector(Acceleration.X, Acceleration.Y, 0.0f).GetSafeNormal();
    if (!AccelerationDirection.IsNearlyZero()) {
      return AccelerationDirection;
    }
  }

  // ③ 兜底：角色正前方
  const AActor *Owner = GetOwner();
  if (Owner == nullptr) {
    return FVector::ForwardVector;
  }
  const FVector Forward = Owner->GetActorForwardVector().GetSafeNormal();
  return Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
}

// ===== 查询 =====

FString USlideComponent::GetSlideDebugString() const {
  const UCharacterMovementComponent *Movement = GetOwnerMovementComponent();

  return FString::Printf(
      TEXT("bIsSliding=%d 表现中=%d bCanSlide=%d 冷却=%.2fs 剩余=%.2fs 累计=%d "
           "本端已滑=%.2fs 进度=%.2f 目标速度=%.0f 当前水平速度=%.0f "
           "移动模式=%d 摩擦=%.2f 最大加速度=%.2f 参数已改写=%d "
           "在播=%s 蒙太奇=%s 本端权威=%d 本地控制=%d"),
      bIsSliding ? 1 : 0, bSlidePresentationActive ? 1 : 0,
      bCanSlide ? 1 : 0, SlideCooldown, GetSlideCooldownRemaining(), SlideCount,
      GetSlideAlpha() * SlideDuration, GetSlideAlpha(), GetDesiredSlideSpeed(),
      GetCurrentHorizontalSpeed(),
      Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1,
      Movement ? Movement->GroundFriction : -1.0f,
      Movement ? Movement->MaxAcceleration : -1.0f,
      RuntimeState.bMovementOverridesApplied ? 1 : 0,
      ActiveSlideMontage ? *ActiveSlideMontage->GetName() : TEXT("None"),
      SlideMontage ? *SlideMontage->GetName() : TEXT("None"),
      IsAuthoritativeForActorComponent(this) ? 1 : 0,
      IsLocallyControlledOwner() ? 1 : 0);
}

bool USlideComponent::IsSlideReady() const {
  if (SlideCooldown <= 0.0f) {
    return true;
  }
  if (!FMath::IsFinite(RuntimeState.LocalSlideStartTime)) {
    return true;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  // LocalSlideStartTime 的初值是 -1000，所以「从没滑过」时这里天然是就绪的
  return (Now - RuntimeState.LocalSlideStartTime) >= SlideCooldown;
}

bool USlideComponent::IsSlideAvailable() const {
  return bCanSlide && !bIsSliding && IsSlideReady();
}

float USlideComponent::GetSlideCooldownRemaining() const {
  if (SlideCooldown <= 0.0f || !FMath::IsFinite(RuntimeState.LocalSlideStartTime)) {
    return 0.0f;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  return FMath::Max(0.0f,
                    SlideCooldown - (Now - RuntimeState.LocalSlideStartTime));
}

float USlideComponent::GetSlideAlpha() const {
  // 只在「本端正在演滑行」时有意义：收尾后一律 0（UI 不会看到一个停在 1 的进度条）
  if (!bSlidePresentationActive || !FMath::IsFinite(RuntimeState.LocalSlideStartTime)) {
    return 0.0f;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  const float Duration = FMath::Max(SlideDuration, KINDA_SMALL_NUMBER);
  return FMath::Clamp((Now - RuntimeState.LocalSlideStartTime) / Duration, 0.0f,
                      1.0f);
}

float USlideComponent::GetDesiredSlideSpeed() const {
  const float StartSpeed =
      (RuntimeState.SlideStartSpeedEffective > 0.0f)
          ? RuntimeState.SlideStartSpeedEffective
          : FMath::Max(SlideStartSpeed, 0.0f);
  const float EndSpeed = FMath::Max(SlideEndSpeed, 0.0f);
  return FMath::Lerp(StartSpeed, EndSpeed, GetSlideAlpha());
}

float USlideComponent::GetCurrentHorizontalSpeed() const {
  const UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr) {
    return 0.0f;
  }
  return FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.0f).Size();
}

// ===== 服务器权威函数 =====

bool USlideComponent::StartSlide_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(false);

  const AActor *Owner = GetOwner();
  const FString Who = Owner ? Owner->GetName() : TEXT("None");
  WFLOG_INFO("[滑行] StartSlide 进入（宿主 %s）：%s", *Who,
             *GetSlideDebugString());

  // 总开关：眩晕 / 死亡等状态把它关掉后一律不生效
  if (!bCanSlide) {
    WFLOG_WARNING("[滑行] 被拒：bCanSlide=false（总开关关着）。宿主 %s", *Who);
    return false;
  }

  UWorld *World = GetWorld();
  if (World == nullptr) {
    WFLOG_WARNING("[滑行] 被拒：World 为空。宿主 %s", *Who);
    return false;
  }

  UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr) {
    WFLOG_WARNING("[滑行] 被拒：宿主 %s 没有 UCharacterMovementComponent，"
                  "滑行没有位移载体。",
                  *Who);
    return false;
  }

  // 重入：上一段滑行还没结束就不接受新的（bIsSliding 只在收尾时置 false）
  if (bIsSliding) {
    WFLOG_WARNING("[滑行] 被拒：已在滑行中（%.2fs 前开始，在播 %s）。宿主 %s",
                  World->GetTimeSeconds() - LastSlideTime,
                  ActiveSlideMontage ? *ActiveSlideMontage->GetName()
                                     : TEXT("None"),
                  *Who);
    return false;
  }

  const float Now = World->GetTimeSeconds();

  // 冷却：用世界时间差，不要用 GetTimerElapsed（掉帧补触发时会给出负值，见 bug-017）
  if (SlideCooldown > 0.0f && (Now - LastSlideTime) < SlideCooldown) {
    WFLOG_INFO("[滑行] 被拒：冷却中（距上次 %.2fs < 冷却 %.2fs，还剩 %.2fs）。宿主 %s",
               Now - LastSlideTime, SlideCooldown,
               SlideCooldown - (Now - LastSlideTime), *Who);
    return false;
  }

  // 只在地面起滑：空中没有「贴地滑」的物理前提（想加空中冲刺请另做能力）
  if (!Movement->IsMovingOnGround()) {
    WFLOG_INFO("[滑行] 被拒：当前不在可行走地面（移动模式=%d，速度=%.0f）。宿主 %s",
               static_cast<int32>(Movement->MovementMode.GetValue()),
               Movement->Velocity.Size(), *Who);
    return false;
  }

  // 起滑速度要求（默认 0 = 站着也能滑）
  if (MinSpeedToStartSlide > 0.0f) {
    const float CurrentSpeed = GetCurrentHorizontalSpeed();
    if (CurrentSpeed < MinSpeedToStartSlide) {
      WFLOG_INFO("[滑行] 被拒：当前水平速度 %.0f < 起滑要求 %.0f。宿主 %s",
                 CurrentSpeed, MinSpeedToStartSlide, *Who);
      return false;
    }
  }

  bIsSliding = true;
  LastSlideTime = Now;
  ++SlideCount;

  WFLOG_INFO("[滑行] 开始：第 %d 次（时长 %.2fs，起始速度下限 %.0f，结束速度 %.0f，"
             "冷却 %.2fs，段名=%s）。%s",
             SlideCount, SlideDuration, SlideStartSpeed, SlideEndSpeed,
             SlideCooldown,
             SlideSectionName.IsNone() ? TEXT("None")
                                       : *SlideSectionName.ToString(),
             *GetSlideDebugString());

  // Multicast 会在服务器本地与所有客户端各执行一次（服务器那次是本地执行，不额外发 RPC），
  // 所以这里**不需要**再单独播一次动画（那会让同一段蒙太奇在服务器上被 Play 两次）。
  //
  // 本端占用角色（OnSlideStarted 广播）与移动参数改写都在它里面完成，顺序在每个端都一致：
  //   播动画 → 广播 Started（订阅者落软锁 / 复位加速）→ 再写滑行用的移动参数。
  // 这个顺序是有意的：让「订阅者的副作用」先发生完，我们再快照 / 改写参数，
  // 免得把别人正要复位的东西快照进我们的还原目标里。
  Multicast_BeginSlide(SlideSectionName);

  if (!bIsSliding) {
    // 保底：Multicast 分支正常情况下不会在同一个端把自己收尾，走到这里说明有意外
    WFLOG_WARNING("[滑行] Multicast_BeginSlide 之后 bIsSliding 已复位，本次滑行没有成立。"
                  "宿主 %s",
                  *Who);
    return false;
  }

  // 位移与结束都由 TickComponent 推进，所以要给它一个兜底：
  // 万一组件 Tick 没在跑（外部关掉了 / 帧更新异常），状态也不能永久挂着。
  OpenSlideTimeout();

  return true;
}

void USlideComponent::StopSlide() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  const AActor *Owner = GetOwner();
  if (!bIsSliding) {
    WFLOG_INFO("[滑行] StopSlide：当前并不在滑行中，空操作（可安全重复调用）。宿主 %s",
               Owner ? *Owner->GetName() : TEXT("None"));
    return;
  }

  WFLOG_INFO("[滑行] StopSlide：提前收尾（松开按键 / 被打断等）。宿主 %s。%s",
             Owner ? *Owner->GetName() : TEXT("None"), *GetSlideDebugString());
  FinishSlide(TEXT("外部提前结束"));
}

void USlideComponent::ResetSlideCooldown() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  LastSlideTime = -1000.0f;
  // 本端那份起滑时间只在没在演的时候清：正演着的时候清掉会让 GetSlideAlpha() 直接跳到 1
  // （速度曲线瞬间掉到 SlideEndSpeed），那不是「清冷却」该有的副作用。
  if (!bSlidePresentationActive) {
    RuntimeState.LocalSlideStartTime = -1000.0f;
  }
  WFLOG_INFO("[滑行] 冷却已清除（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
}

// ===== 权威端每帧推进 =====

void USlideComponent::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // 不在滑行中：一次分支就退出（组件 Tick 常开的理由见头文件）
  if (!bSlidePresentationActive) {
    return;
  }

  // 位移：权威端 = 玩法模拟；本地控制的自主代理 = 本地预测镜像。
  // 两端跑的是同一条曲线，所以预测出来的速度不会和服务器打架。
  if (ShouldDriveSlideLocally()) {
    ApplySlideSpeedCurve(DeltaTime);
  }

  // 结束时机**只由权威端判**：客户端等 Multicast_EndSlide。
  // （客户端自己判会在 RPC 延迟里出现「我这端已经结束了、服务器还在滑」的分歧。）
  if (IsAuthoritativeForActorComponent(this)) {
    TickAuthoritySlide();
  }
}

void USlideComponent::TickAuthoritySlide() {
  UWorld *World = GetWorld();
  if (World == nullptr) {
    return;
  }

  // 总开关被关掉（被眩晕 / 死亡等）：只挡新请求不够，已经在滑的也要立刻收尾
  if (!bCanSlide) {
    WFLOG_WARNING("[滑行] 滑行中 bCanSlide 被关掉，立刻收尾。%s",
                  *GetSlideDebugString());
    FinishSlide(TEXT("总开关 bCanSlide 被关掉"));
    return;
  }

  const UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr) {
    WFLOG_WARNING("[滑行] 滑行中移动组件消失了，立刻收尾。%s",
                  *GetSlideDebugString());
    FinishSlide(TEXT("移动组件消失"));
    return;
  }

  // 离地（跳跃 / 掉下平台 / 被顶起）：贴地滑的模型已经不成立
  if (bEndSlideWhenAirborne && !Movement->IsMovingOnGround()) {
    WFLOG_INFO("[滑行] 离地（移动模式=%d，速度 %.0f），结束滑行。%s",
               static_cast<int32>(Movement->MovementMode.GetValue()),
               Movement->Velocity.Size(), *GetSlideDebugString());
    FinishSlide(TEXT("离地（跳跃 / 掉落）"));
    return;
  }

  const float Elapsed = World->GetTimeSeconds() - LastSlideTime;

  // 时长到期：正常结束路径
  if (Elapsed >= SlideDuration) {
    WFLOG_INFO("[滑行] 时长到期（%.2fs >= %.2fs），结束滑行。%s", Elapsed,
               SlideDuration, *GetSlideDebugString());
    FinishSlide(TEXT("滑行时长到期"));
    return;
  }

  // 速度过低：撞墙 / 上坡 / 被夹住。宽限时间是为了不误伤起手那一两帧。
  if (SlideMinSpeedToContinue > 0.0f && Elapsed >= SlideMinSpeedGraceTime) {
    const float CurrentSpeed = GetCurrentHorizontalSpeed();
    if (CurrentSpeed < SlideMinSpeedToContinue) {
      WFLOG_INFO("[滑行] 速度 %.0f 低于继续阈值 %.0f（撞墙 / 上坡 / 被夹住），"
                 "结束滑行。%s",
                 CurrentSpeed, SlideMinSpeedToContinue,
                 *GetSlideDebugString());
      FinishSlide(TEXT("速度低于继续阈值"));
      return;
    }
  }
}

void USlideComponent::ApplySlideSpeedCurve(float DeltaTime) {
  // 本函数逐帧执行，**刻意不打日志**（一条滑行 60 条日志会把真正有用的判定点淹掉）。
  // 需要看它干了什么时读 GetSlideDebugString()：里面已经有目标速度 / 当前速度 / 参数是否已改写。
  //
  // 时序说明：本组件与 `UCharacterMovementComponent` 都在 `TG_PrePhysics`，谁先跑由注册顺序决定
  // （两者都是构造函数里创建的，顺序稳定但不由我们保证）。这里写的 `Velocity` 被「本帧稍后
  // 或下一帧」的移动模拟读取，对一条 1 秒的曲线来说最多差一帧——代价可以接受，
  // 换来的是**不需要**去动 tick 顺序（`AddPrerequisite` 那套在运行时很容易出错）。
  UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr) {
    return;
  }

  const float CurrentSpeed = GetCurrentHorizontalSpeed();
  const float DesiredSpeed = GetDesiredSlideSpeed();

  // **只往下夹**：曲线在正常滑行里是「越来越慢」，我们负责把多余的速度削掉；
  // 速度已经低于曲线（撞墙、上坡）时不去把它抬回来 —— 那会让角色顶着墙继续加速。
  if (CurrentSpeed <= DesiredSpeed + KINDA_SMALL_NUMBER) {
    return;
  }

  float NewSpeed = DesiredSpeed;
  if (SlideMaxSpeedDecelRate > 0.0f) {
    // 限速下降：带着外部高速（击飞 / 爆炸）起滑时不会被曲线一刀切成低速
    NewSpeed = FMath::Max(DesiredSpeed,
                          CurrentSpeed - SlideMaxSpeedDecelRate * DeltaTime);
  }
  if (NewSpeed >= CurrentSpeed) {
    return;
  }

  const FVector HorizontalVelocity(Movement->Velocity.X, Movement->Velocity.Y,
                                   0.0f);
  const FVector Direction = HorizontalVelocity.GetSafeNormal();
  if (Direction.IsNearlyZero()) {
    // 水平速度小到取不出方向：不碰它（写回去只会引入噪声）
    return;
  }

  const FVector NewHorizontalVelocity = Direction * NewSpeed;
  Movement->Velocity.X = NewHorizontalVelocity.X;
  Movement->Velocity.Y = NewHorizontalVelocity.Y;
  // Z 分量不动：下落 / 台阶的竖直速度不归我们管
}

// ===== 移动参数改写（本地预测镜像）=====

void USlideComponent::ApplySlideMovementOverrides() {
  UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr) {
    return;
  }
  if (RuntimeState.bMovementOverridesApplied) {
    // 幂等：已经在生效就别再快照一次（第二次快照存下来的会是**滑行值**，
    // 收尾时就会把溜冰参数「还原」回去）
    return;
  }

  FSlideRuntimeState::FMovementSnapshot &Snapshot =
      RuntimeState.MovementSnapshot;
  Snapshot.GroundFriction = Movement->GroundFriction;
  Snapshot.BrakingDecelerationWalking = Movement->BrakingDecelerationWalking;
  Snapshot.BrakingFriction = Movement->BrakingFriction;
  Snapshot.MaxAcceleration = Movement->MaxAcceleration;
  Snapshot.bValid = true;

  Movement->GroundFriction = FMath::Max(0.0f, SlideGroundFriction);
  Movement->BrakingDecelerationWalking = FMath::Max(0.0f, SlideBrakingDeceleration);
  // BrakingFriction 只在 bUseSeparateBrakingFriction 为真时才参与计算
  // （CharacterMovementComponent.h:309-318），一并清零：项目若哪天把它打开，
  // 滑行的「零摩擦」不该被这一项悄悄破坏。
  Movement->BrakingFriction = 0.0f;
  Movement->MaxAcceleration =
      bAllowSteering ? FMath::Max(0.0f, SlideMaxAcceleration) : 0.0f;

  RuntimeState.bMovementOverridesApplied = true;

  WFLOG_INFO("[滑行] 已改写本端移动参数（摩擦 %.2f→%.2f，刹车减速 %.2f→%.2f，"
             "BrakingFriction %.2f→0.00，最大加速度 %.2f→%.2f，转向=%d）。本端权威=%d",
             Snapshot.GroundFriction, Movement->GroundFriction,
             Snapshot.BrakingDecelerationWalking,
             Movement->BrakingDecelerationWalking, Snapshot.BrakingFriction,
             Snapshot.MaxAcceleration, Movement->MaxAcceleration,
             bAllowSteering ? 1 : 0,
             IsAuthoritativeForActorComponent(this) ? 1 : 0);
}

void USlideComponent::RestoreSlideMovementOverrides(const TCHAR *Reason) {
  UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr || !RuntimeState.bMovementOverridesApplied) {
    return;
  }

  const FSlideRuntimeState::FMovementSnapshot &Snapshot =
      RuntimeState.MovementSnapshot;
  if (!Snapshot.bValid) {
    // 不该发生：bMovementOverridesApplied 与 bValid 是同时置位的。
    // 真出现了也不能瞎写（写回未捕获的 0 会把角色永久钉死 / 变成溜冰，见 bug-027），
    // 所以只报错并清标志。
    WFLOG_ERROR("[滑行] 移动参数快照无效却要求还原（%s），拒绝写回以免把角色钉死。%s",
                Reason, *GetSlideDebugString());
    RuntimeState.bMovementOverridesApplied = false;
    return;
  }

  Movement->GroundFriction = Snapshot.GroundFriction;
  Movement->BrakingDecelerationWalking = Snapshot.BrakingDecelerationWalking;
  Movement->BrakingFriction = Snapshot.BrakingFriction;
  Movement->MaxAcceleration = Snapshot.MaxAcceleration;
  RuntimeState.bMovementOverridesApplied = false;

  WFLOG_INFO("[滑行] 已还原本端移动参数（%s）：摩擦=%.2f，刹车减速=%.2f，"
             "BrakingFriction=%.2f，最大加速度=%.2f。本端权威=%d",
             Reason, Snapshot.GroundFriction, Snapshot.BrakingDecelerationWalking,
             Snapshot.BrakingFriction, Snapshot.MaxAcceleration,
             IsAuthoritativeForActorComponent(this) ? 1 : 0);
}

float USlideComponent::ApplySlideImpulse() {
  UCharacterMovementComponent *Movement = GetOwnerMovementComponent();
  if (Movement == nullptr) {
    return 0.0f;
  }

  const float CurrentSpeed = GetCurrentHorizontalSpeed();
  // 动量优先：冲刺中起滑不会被拉到 SlideStartSpeed 这个下限上
  const float StartSpeed =
      FMath::Max(CurrentSpeed, FMath::Max(SlideStartSpeed, 0.0f));
  const FVector Direction = ResolveSlideDirection();

  Movement->Velocity.X = Direction.X * StartSpeed;
  Movement->Velocity.Y = Direction.Y * StartSpeed;
  // Z 不动：保住竖直速度

  WFLOG_INFO("[滑行] 起手冲量：方向 %.2f,%.2f，速度 %.0f→%.0f（原水平速度 %.0f，"
             "下限 %.0f）。本端权威=%d",
             Direction.X, Direction.Y, CurrentSpeed, StartSpeed, CurrentSpeed,
             SlideStartSpeed, IsAuthoritativeForActorComponent(this) ? 1 : 0);

  return StartSpeed;
}

// ===== 蒙太奇 =====

bool USlideComponent::PlaySlideMontageInternal(FName InSectionName) {
  // 蒙太奇为空是**合法状态**（不是配置错误）：滑行的位移与时长由本组件推进，
  // 动画只是表现。所以这里记 INFO 而不是 WARNING，返回值 false 也只表示「没动画」。
  if (SlideMontage == nullptr) {
    WFLOG_INFO("[滑行] 未配置 SlideMontage：本次滑行没有动画表现"
               "（位移、状态、冷却照常）。宿主 %s",
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    ActiveSlideMontage = nullptr;
    return false;
  }

  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  if (OwnerChar == nullptr) {
    WFLOG_WARNING("[滑行] 播放失败：宿主 %s 不是 ACharacter。",
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    ActiveSlideMontage = nullptr;
    return false;
  }

  USkeletalMeshComponent *Mesh = OwnerChar->GetMesh();
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    WFLOG_WARNING("[滑行] 播放失败：宿主 %s 没有 AnimInstance（Mesh=%s）。",
                  *OwnerChar->GetName(),
                  Mesh ? *Mesh->GetName() : TEXT("None"));
    ActiveSlideMontage = nullptr;
    return false;
  }

  // 用返回值判成败：<= 0 表示引擎拒播（该蒙太奇不在当前状态机里等）
  const float Rate = FMath::Max(MontagePlayRate, KINDA_SMALL_NUMBER);
  const float PlayLength = AnimInst->Montage_Play(SlideMontage, Rate);
  if (PlayLength <= 0.0f) {
    WFLOG_WARNING("[滑行] %s 播放失败（Montage_Play 返回 %.2f，播速 %.2f，宿主 %s）。",
                  *SlideMontage->GetName(), PlayLength, Rate,
                  *OwnerChar->GetName());
    ActiveSlideMontage = nullptr;
    return false;
  }

  if (!InSectionName.IsNone()) {
    AnimInst->Montage_JumpToSection(InSectionName, SlideMontage);
    WFLOG_INFO("[滑行] 已跳到 Section %s。", *InSectionName.ToString());
  }

  // 记「本段实际在播哪个蒙太奇」：收尾的 Montage_Stop 只认它，
  // 不要用配置项代替（配置可能被热改，动画蓝图也可能换了实例）。
  ActiveSlideMontage = SlideMontage;

  WFLOG_INFO("[滑行] 已播放 %s（时长 %.2fs，播速 %.2f，Section=%s，宿主 %s）。"
             "本端权威=%d",
             *SlideMontage->GetName(), SlideMontage->GetPlayLength(), Rate,
             InSectionName.IsNone() ? TEXT("None")
                                    : *InSectionName.ToString(),
             *OwnerChar->GetName(),
             IsAuthoritativeForActorComponent(this) ? 1 : 0);
  return true;
}

void USlideComponent::StopActiveSlideMontage() {
  if (ActiveSlideMontage == nullptr) {
    return;
  }

  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  USkeletalMeshComponent *Mesh = OwnerChar ? OwnerChar->GetMesh() : nullptr;
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst != nullptr) {
    const float BlendOut = FMath::Max(MontageStopBlendOutTime, 0.0f);
    AnimInst->Montage_Stop(BlendOut, ActiveSlideMontage);
    WFLOG_INFO("[滑行] 已停掉蒙太奇 %s（%.2fs 淡出）。", *ActiveSlideMontage->GetName(),
               BlendOut);
  } else {
    WFLOG_WARNING("[滑行] 想停掉蒙太奇 %s，但拿不到 AnimInstance（宿主 %s）。",
                  *ActiveSlideMontage->GetName(),
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  }

  ActiveSlideMontage = nullptr;
}

// ===== 收尾 =====

void USlideComponent::OpenSlideTimeout() {
  UWorld *World = GetWorld();
  if (World == nullptr) {
    return;
  }

  // 兜底时长 = 滑行时长 + 余量。余量是给「收尾那一帧掉帧 / 蓝图里延迟 StopSlide」留的，
  // 太小会在正常滑行时误触发强制收尾。
  const float TimeoutSeconds =
      FMath::Max(SlideDuration, 0.0f) + SlideTimeoutPaddingSeconds;

  FTimerDelegate TimeoutDelegate;
  TimeoutDelegate.BindUObject(this, &USlideComponent::HandleSlideTimeout);
  World->GetTimerManager().SetTimer(SlideTimeoutHandle, TimeoutDelegate,
                                    TimeoutSeconds, /*bLoop=*/false);

  WFLOG_INFO("[滑行] 已挂兜底定时器：%.2fs 后若滑行仍未结束就强制收尾"
             "（时长 %.2f + %.2f 余量）。%s",
             TimeoutSeconds, SlideDuration, SlideTimeoutPaddingSeconds,
             *GetSlideDebugString());
}

void USlideComponent::HandleSlideTimeout() {
  // 定时器只在权威端挂（见 StartSlide_Implementation）
  if (!IsAuthoritativeForActorComponent(this)) {
    return;
  }

  if (!bIsSliding) {
    // 正常结束后定时器本该被清掉；走到这里说明清理路径漏了，只记一条 INFO
    WFLOG_INFO("[滑行] 兜底定时器触发，但当前并不在滑行中（可能已被正常收尾）。%s",
               *GetSlideDebugString());
    return;
  }

  WFLOG_WARNING(
      "[滑行] 滑行超时未结束，强制收尾（TickComponent 没在跑？"
      "PrimaryComponentTick 被外部关掉 / 组件被暂停？）。%s",
      *GetSlideDebugString());
  FinishSlide(TEXT("兜底超时"));
}

void USlideComponent::FinishSlide(const TCHAR *Reason) {
  // 只有权威端能收尾：它既复位玩法状态，也是「把收尾同步到所有端」的入口
  WF_COMPONENT_AUTHORITY_GUARD(void());

  if (!bIsSliding && !bSlidePresentationActive) {
    WFLOG_INFO("[滑行] FinishSlide(%s)：当前并不在滑行中，空操作。宿主 %s", Reason,
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return;
  }

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(SlideTimeoutHandle);
  }

  const bool bWasSliding = bIsSliding;
  const float Elapsed =
      (GetWorld() != nullptr)
          ? GetWorld()->GetTimeSeconds() - LastSlideTime
          : -1.0f;

  // 玩法状态：只有权威端写
  bIsSliding = false;

  WFLOG_INFO("[滑行] 结束：%s（本次已滑 %.2fs，原 bIsSliding=%d，累计 %d 次，"
             "本端表现中=%d）。%s",
             Reason, Elapsed, bWasSliding ? 1 : 0, SlideCount,
             bSlidePresentationActive ? 1 : 0, *GetSlideDebugString());

  // 表现收尾在每个端各自执行：停动画 + 还原移动参数 + 广播 OnSlideFinished。
  // 服务器上这一次调用就是「本地执行」（Multicast 的 callspace 含 Local），
  // 所以权威端不需要再单独收一次尾——那样会把 Finished 广播两遍。
  Multicast_EndSlide();
}

// ===== 表现同步 =====

void USlideComponent::Multicast_BeginSlide_Implementation(FName InSectionName) {
  const bool bAuthority = IsAuthoritativeForActorComponent(this);

  // ⚠️ 本函数在**每个端**都会执行一次，而且「客户端自己调用」时也只会在本端执行
  // （引擎行为：Multicast 在客户端的 callspace 是 Local，既不发服务器也不广播给别人，
  // 见头文件里对 `AActor::GetFunctionCallspace` 的引用）。
  // 所以它必须对「任何端、任何调用来源」都安全：只播动画 + 维护本端表现标志与
  // **本地预测**用的移动参数，不写任何玩法状态（bIsSliding / LastSlideTime 只由权威端写）。
  WFLOG_INFO("[滑行] Multicast_BeginSlide 到达（%s）：Section=%s。%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             InSectionName.IsNone() ? TEXT("None")
                                    : *InSectionName.ToString(),
             *GetSlideDebugString());

  if (bSlidePresentationActive) {
    WFLOG_WARNING("[滑行] 本端已经在演滑行，忽略这次重复的 Multicast_BeginSlide"
                  "（否则 Started 会广播两次，订阅者的账就配不平了）。%s",
                  *GetSlideDebugString());
    return;
  }

  // 本端表现生命周期开始：**不看蒙太奇是否播起来**。
  // 这点与翻滚不同：翻滚的结束条件就是蒙太奇结束，所以它必须靠「表现真的播起来」来配对；
  // 滑行的结束条件是时长 / 速度 / 离地，由本组件自己推进，蒙太奇为空也照样成立。
  const bool bPlayed = PlaySlideMontageInternal(InSectionName);
  if (!bPlayed) {
    WFLOG_INFO("[滑行] 本端没有滑行动画（蒙太奇未配置 / 播不起来），"
               "滑行照常推进（位移由组件负责）。");
  }

  UWorld *World = GetWorld();
  RuntimeState.LocalSlideStartTime =
      (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  RuntimeState.SlideStartSpeedEffective = 0.0f;
  bSlidePresentationActive = true;

  // 先广播，再改写移动参数：订阅者（APlayerCharacter）收到 Started 会落软锁并复位加速，
  // 那是**别人的副作用**，让它先做完，我们再快照 / 改写自己负责的参数。
  // 顺序反了的话，别人的复位会落在我们的快照之后，我们收尾时就会把过期值写回去。
  OnSlideStarted.Broadcast();
  WFLOG_INFO("[滑行] %s 已广播 OnSlideStarted（订阅者会落软锁）。%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             *GetSlideDebugString());

  // 位移只在本端确实要驱动它时才写（权威端 / 本地控制的自主代理，见类注释）
  if (ShouldDriveSlideLocally()) {
    ApplySlideMovementOverrides();
    RuntimeState.SlideStartSpeedEffective = ApplySlideImpulse();
  } else {
    WFLOG_INFO("[滑行] 本端不驱动位移（非权威且非本地控制），只播表现。%s",
               *GetSlideDebugString());
  }
}

void USlideComponent::Multicast_EndSlide_Implementation() {
  const bool bAuthority = IsAuthoritativeForActorComponent(this);
  const bool bWasPresenting = bSlidePresentationActive;

  // ⚠️ 这里同样**不能**加「非权威端就忽略」的门禁：客户端收到它就是正常接收，
  // 加门禁等于客户端永远收不了尾（bug-030 踩过）。
  StopActiveSlideMontage();
  RestoreSlideMovementOverrides(TEXT("滑行收尾"));
  bSlidePresentationActive = false;
  RuntimeState.SlideStartSpeedEffective = 0.0f;

  WFLOG_INFO("[滑行] Multicast_EndSlide 到达（%s）：本端表现中=%d → 已收尾。%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             bWasPresenting ? 1 : 0, *GetSlideDebugString());

  // Started / Finished 严格配对：只有本端确实广播过 Started 才广播 Finished。
  // 不配对时宁可少广播一次，也不要让订阅者收到「无配对的解锁请求」——
  // 那会让移动锁的持有者集合与实际状态不一致，排查时反而更乱。
  if (bWasPresenting) {
    OnSlideFinished.Broadcast();
    WFLOG_INFO("[滑行] 已广播 OnSlideFinished（订阅者会释放移动锁）。");
  }
}

void USlideComponent::Multicast_PlaySlideEffects_Implementation() {
  // ⚠️ 这里**不能**加「非权威端就忽略」的门禁：Multicast 在每个端都会执行，
  // 客户端收到的那一次本来就不是权威端——加了门禁等于客户端永远看不到特效。
  // 表现同步要的就是「每个端各自本地播一遍」。
  WFLOG_INFO("[滑行] 广播滑行表现（PlaySlideEffects 蓝图事件，本端权威=%d）。宿主 %s",
             IsAuthoritativeForActorComponent(this) ? 1 : 0,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  PlaySlideEffects();
}

void USlideComponent::PlaySlideEffects_Implementation() {
  // 本地表现入口：蓝图覆写本事件即可（尘土 / 音效 / 镜头抖动）。C++ 默认什么都不做。
}

// ===== 客户端 -> 服务器 =====

bool USlideComponent::Server_StartSlide_Validate() {
  // 无参数，没有可校验的输入；真正的重入 / 冷却 / 总开关判定在 StartSlide() 内部
  return true;
}

void USlideComponent::Server_StartSlide_Implementation() {
  // 服务器（含单机 / 监听服务器）上 Server RPC 就地同步执行，
  // 所以单机与联机走的是同一条路径，不需要写 HasAuthority() 分支。
  WFLOG_INFO("[滑行] RPC Server_StartSlide 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  StartSlide();
}

bool USlideComponent::Server_StopSlide_Validate() {
  // 无参数，恒为 true；「没在滑行」是可接受的输入（StopSlide 内部会当空操作）
  return true;
}

void USlideComponent::Server_StopSlide_Implementation() {
  WFLOG_INFO("[滑行] RPC Server_StopSlide 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  StopSlide();
}
