// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/SprintBoostComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"
#include "Utils/ComponentAuthorityGuard.h"
#include "Utils/WildforgeLog.h"

// ===== 速度提升（长按加速）=====
//
// 设计要点（都是从旧实现踩过来的，改动前先读一遍）：
//   1. 真正的权威状态只有「CharacterMovement 的 MaxWalkSpeed」一个，它由
//      UCharacterMovementComponent 自带复制（OnRep_MaxWalkSpeed）——不需要我们再复制
//      一份，客户端本地移动与服务器校验用的是同一个值。
//   2. 因此本能力全部在权威端跑：客户端自己算一套速度只会和服务器打架（客户端改自己的
//      MaxWalkSpeed 既留不住、又会让服务器回滚），所以客户端只能发 Server_* 请求。
//   3. 基准速度（BaseMaxWalkSpeed）只在「未加速」时抓取，保证不会被加速后的值污染。
//   4. 每帧刷新必须用 SetTimerForNextTick 链：SetTimer 的 InRate <= 0 是「清掉定时器」
//      的意思，一次都不会触发（bug-017）。

// Sets default values for this component's properties
USprintBoostComponent::USprintBoostComponent() {
  // 加速靠定时器推进，不需要组件自己 Tick；只有开了本地驱动才逐帧跑（BeginPlay 里开）
  PrimaryComponentTick.bCanEverTick = true;
  PrimaryComponentTick.bStartWithTickEnabled = false;

  // 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）。
  // 缺了这行 DOREPLIFETIME_* 等于没写——bBoostActive / BoostAlpha 到不了拥有者客户端。
  SetIsReplicatedByDefault(true);
}

void USprintBoostComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 这三个都是「服务器写、客户端只读」的状态，用 COND_OwnerOnly 复制给拥有者：
  // 冲刺条 / FOV 这类表现只有本人看得到，没必要发给其他端。
  //
  // ⚠️ 不要改成 COND_SimulatedOnly：那个条件**不会**发给自主代理
  // （AutonomousProxy，也就是本地玩家自己），恰好是最需要这两个值的那个端。
  DOREPLIFETIME_CONDITION(USprintBoostComponent, bBoostActive, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(USprintBoostComponent, BoostAlpha, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(USprintBoostComponent, LastTargetSpeed,
                          COND_OwnerOnly);
}

void USprintBoostComponent::BeginPlay() {
  Super::BeginPlay();

  // 这里刻意**不**抓基准速度：客户端上的 MaxWalkSpeed 要等复制到位才是有效值，
  // 在 BeginPlay 抓会把默认值（甚至是 0）记成基准，之后 ResetMaxSpeed 就还原不回去了。
  // 基准值只在权威端、且只在自己即将开始加速时抓（StartSpeedBoost 内）。

  SetComponentTickEnabled(bDriveOwnerLocally);

  // ⚠️ 这里必须打印**移动组件真实的** MaxWalkSpeed，不能打印
  //    `BoostState.BaseMaxWalkSpeed` —— 后者在第一次加速之前一直是 0（刻意未捕获），
  //    打出来会让人误以为「角色速度是 0 / 角色不能动」，本项目排查 bug-027 时就被它
  //    误导过一轮。基准确认由 bBaseSpeedValid 表达，不再靠数值猜。
  const UCharacterMovementComponent *Movement =
      GetOwner() ? GetOwner()->FindComponentByClass<UCharacterMovementComponent>()
                 : nullptr;

  WFLOG_INFO(
      "[加速] 组件就绪：宿主 %s，本端权威=%d，当前 MaxWalkSpeed=%.2f"
      "（蹲伏 %.2f），基准值=%s，本地驱动=%d。",
      GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
      IsAuthoritativeForActorComponent(this) ? 1 : 0,
      Movement ? Movement->MaxWalkSpeed : -1.0f,
      Movement ? Movement->MaxWalkSpeedCrouched : -1.0f,
      BoostState.bBaseSpeedValid
          ? *FString::Printf(TEXT("已捕获 %.2f/%.2f 比例 %.3f"),
                             BoostState.BaseMaxWalkSpeed,
                             BoostState.BaseMaxWalkSpeedCrouched,
                             BoostState.CrouchSpeedRatio)
          : TEXT("尚未捕获（第一次加速开始时才抓，这是设计内的状态）"),
      bDriveOwnerLocally ? 1 : 0);
}

void USprintBoostComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // next-tick 链的回调绑在 this 上，组件销毁时必须主动清掉，
  // 否则回调可能打到「正在销毁」的对象上。
  ClearSprintBoostTimer();
  Super::EndPlay(EndPlayReason);
}

void USprintBoostComponent::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // 本地驱动模式：客户端也把曲线跑一遍（只更新进度、广播，不写移动组件）
  TickLocalBoostVisualOnly();
}

// 加速状态的一行快照：曲线没反应时能直接从日志看出卡在哪一步
// （RPC 有没有到服务器 → 参数有没有被拒 → 定时器有没有挂上）。
FString USprintBoostComponent::GetSprintBoostDebugString() const {
  const UCharacterMovementComponent *Movement =
      GetOwner() ? GetOwner()->FindComponentByClass<UCharacterMovementComponent>()
                 : nullptr;

  // 方向门控的判定结果：日志里能直接看到「为什么这次加速被收掉了」
  // （夹角多少、允许多少、是没输入还是非步行）。
  const FSprintDirectionInfo DirectionInfo = EvaluateSprintDirection();
  const FString DirectionText = FString::Printf(
      TEXT("方向=%s 夹角=%.1f° 允许=%.1f° 有输入=%d 参考=角色正前方"),
      DirectionInfo.bInputDirectionIsForward ? TEXT("向前") : TEXT("非向前"),
      DirectionInfo.InputAngleDegrees, MaxForwardSprintAngle,
      DirectionInfo.bHasMoveInput ? 1 : 0);

  return FString::Printf(
      TEXT("bBoostActive=%d Alpha=%.3f 起=%.2f 目标=%.2f 基准=%.2f/%.2f(可信=%d) "
           "比例=%.3f 已跑=%.3fs/%.3fs 模式=%s 代次=%u 当前MaxWalkSpeed=%.2f %s"),
      BoostState.bBoostActive ? 1 : 0, GetSpeedBoostAlpha(),
      BoostState.StartSpeed, BoostState.TargetSpeed,
      BoostState.BaseMaxWalkSpeed, BoostState.BaseMaxWalkSpeedCrouched,
      BoostState.bBaseSpeedValid ? 1 : 0, BoostState.CrouchSpeedRatio,
      BoostState.ElapsedTime, BoostState.BoostDuration,
      BoostState.bIntervalTimer ? TEXT("固定间隔") : TEXT("每帧(next-tick)"),
      BoostState.Generation, Movement ? Movement->MaxWalkSpeed : -1.0f,
      *DirectionText);
}

void USprintBoostComponent::CaptureBaseMaxSpeed() {
  const AActor *Owner = GetOwner();
  const UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  if (Movement == nullptr) {
    WFLOG_WARNING("[加速] CaptureBaseMaxSpeed 失败：宿主 %s 上没有 "
                  "CharacterMovementComponent，基准速度保持 %.2f/%.2f。",
                  Owner ? *Owner->GetName() : TEXT("None"),
                  BoostState.BaseMaxWalkSpeed,
                  BoostState.BaseMaxWalkSpeedCrouched);
    return;
  }

  BoostState.BaseMaxWalkSpeed = Movement->MaxWalkSpeed;
  BoostState.BaseMaxWalkSpeedCrouched = Movement->MaxWalkSpeedCrouched;
  // 捕获成功才把标志打开：`ResetMaxSpeed` 靠它决定「能不能写回基准值」。
  BoostState.bBaseSpeedValid = true;

  // 蹲伏 / 步行的基准比例（UE 默认 600/300 = 0.5）。加速时按它同步缩放蹲伏速度，
  // 基准步行速度为 0 时退回 1.0（避免除零，也不会把蹲伏速度压成 0）。
  BoostState.CrouchSpeedRatio =
      (Movement->MaxWalkSpeed > KINDA_SMALL_NUMBER)
          ? (Movement->MaxWalkSpeedCrouched / Movement->MaxWalkSpeed)
          : 1.0f;
  BoostState.CrouchSpeedRatio = FMath::Max(BoostState.CrouchSpeedRatio, 0.0f);
}

void USprintBoostComponent::ApplyMaxWalkSpeed(float InMaxWalkSpeed, float Alpha) {
  AActor *Owner = GetOwner();
  UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  if (Movement == nullptr) {
    WFLOG_WARNING("[加速] ApplyMaxWalkSpeed 失败：宿主 %s 上没有移动组件。",
                  Owner ? *Owner->GetName() : TEXT("None"));
    return;
  }

  // 兜底闸门：绝不把一个「会让角色走不动」的速度写进移动组件。
  // 这条闸门是 bug-027 的补丁——那次是 ResetMaxSpeed 在基准值尚未捕获（= 0）时
  // 无条件写回，把角色钉死在原地。速度提升本身不可能需要 <= 0 的值，
  // 所以这里拒掉是安全的，而且能在日志里立刻看到是哪个调用点干的。
  if (!FMath::IsFinite(InMaxWalkSpeed) || InMaxWalkSpeed <= 0.0f) {
    WFLOG_ERROR("[加速] ApplyMaxWalkSpeed 被拒：速度 %.3f <= 0 或非有限值，"
                "写进去角色会完全走不动。宿主 %s（当前 MaxWalkSpeed=%.2f，"
                "基准=%.2f，基准是否可信=%d）",
                InMaxWalkSpeed, Owner ? *Owner->GetName() : TEXT("None"),
                Movement->MaxWalkSpeed, BoostState.BaseMaxWalkSpeed,
                BoostState.bBaseSpeedValid ? 1 : 0);
    return;
  }

  Movement->MaxWalkSpeed = InMaxWalkSpeed;
  // 蹲伏上限按基准比例跟着抬（600/300 的配置加速到 900 时蹲伏是 450），
  // 而不是直接塞成和步行一样快——那样加速中蹲下就没有任何代价了。
  Movement->MaxWalkSpeedCrouched = InMaxWalkSpeed * BoostState.CrouchSpeedRatio;

  BoostAlpha = Alpha;
  OnSprintBoostUpdated.Broadcast(Alpha);
}

void USprintBoostComponent::ClearSprintBoostTimer() {
  if (const UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(BoostState.TimerHandle);
  }
  BoostState.TimerHandle.Invalidate();
  BoostState.bIntervalTimer = false;
}

// 方向门控的判定核心。三处消费者（门控 / 调试串 / 蓝图查询）都走这里，
// 保证「日志里说向前」和「门控认为向前」永远是同一个结论。
//
// ⚠️ 读 `GetCurrentAcceleration()` 而不是 `GetLastInputVector()`：
//    后者在服务器上恒为零向量（`APawn::LastControlInputVector` 只由客户端的
//    `AddMovementInput` 累加，Pawn.cpp:819-822）；服务器拿到的是 move 包里的
//    `Acceleration`（`MoveAutonomous`：CharacterMovementComponent.cpp:10512）。
USprintBoostComponent::FSprintDirectionInfo
USprintBoostComponent::EvaluateSprintDirection() const {
  FSprintDirectionInfo Info;

  // 1) 功能关着：不看方向
  if (!bSprintOnlyForward) {
    Info.Reason = TEXT("方向门控已关闭");
    return Info;
  }

  const AActor *Owner = GetOwner();
  const UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  if (Movement == nullptr) {
    // 调用方（TickSprintBoost）会单独记一条 WARNING，这里只保证不误收加速
    Info.Reason = TEXT("宿主上没有移动组件");
    return Info;
  }

  // 2) 非步行状态放行：跳跃 / 下落中不该因为读不到移动输入就把加速收掉
  //    （UE 默认 AirControl = 0.05，空中按方向键几乎不产生 Acceleration）。
  //    落地回到 MOVE_Walking 后立即恢复判定。
  const TEnumAsByte<EMovementMode> Mode = Movement->MovementMode;
  if (Mode != MOVE_Walking) {
    Info.Reason = FString::Printf(
        TEXT("移动模式=%s，空中/非步行不判定"), *UEnum::GetValueAsString(Mode));
    return Info;
  }

  // 3) 方向输入：世界空间的加速度方向。
  //    ⚠️ 不能用 `Acceleration.Size()` 判断「有没有输入」——它不是输入长度：
  //    `ScaleInputAcceleration` 会在无输入但仍有速度时把它塞成 `Velocity.GetSafeNormal()`
  //    （CharacterMovementComponent.cpp:3810-3820），长度恒为 1。
  //    所以只看方向，不把长度当强度。
  const FVector InputDirection = Movement->GetCurrentAcceleration();
  // 用 `.Size()` 而不是 `.SizeSquared()` 与阈值比：阈值是**长度**语义（暴露给编辑器调的那个数），
  // 拿 SizeSquared 去比会把实际生效的阈值变成「阈值的平方」，配置和表现对不上。
  if (InputDirection.Size() <= SprintDirectionInputThreshold) {
    // 4) 没有方向输入：放行（先按 Shift 再按 W 是常见操作顺序）
    Info.Reason = TEXT("本帧没有移动输入");
    return Info;
  }

  Info.bHasMoveInput = true;

  // 夹角：输入方向 与 角色正前方。两边都可能不是单位向量，先各自归一化。
  const FVector ForwardDirection =
      Owner->GetActorForwardVector().GetSafeNormal();
  const FVector NormalizedInput = InputDirection.GetSafeNormal();
  if (ForwardDirection.IsNearlyZero() || NormalizedInput.IsNearlyZero()) {
    Info.Reason = TEXT("方向向量退化（长度过小），放行");
    return Info;
  }

  const float CosAngle =
      FMath::Clamp(ForwardDirection | NormalizedInput, -1.0f, 1.0f);
  Info.InputAngleDegrees = FMath::RadiansToDegrees(FMath::Acos(CosAngle));

  const float AllowedAngle =
      FMath::Clamp(MaxForwardSprintAngle, 0.0f, 180.0f);
  Info.bInputDirectionIsForward = Info.InputAngleDegrees <= AllowedAngle;

  Info.Reason = FString::Printf(
      TEXT("%s（夹角 %.1f°/允许 %.1f°）"),
      Info.bInputDirectionIsForward ? TEXT("向前") : TEXT("非向前"),
      Info.InputAngleDegrees, AllowedAngle);
  return Info;
}

void USprintBoostComponent::TickSprintBoost() {
  // ApplyMaxWalkSpeed 会广播 OnSprintBoostUpdated，蓝图有机会在里面 Reset 或重开加速，
  // 所以先记下本次回调属于哪一代，续挂定时器之前用它确认状态没被改过。
  const uint32 MyGeneration = BoostState.Generation;

  AActor *Owner = GetOwner();
  UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;

  if (Movement == nullptr || !BoostState.bBoostActive) {
    // 移动组件没了，或加速已被 ResetMaxSpeed 结束：定时器没有继续存在的意义
    if (Movement == nullptr && BoostState.bBoostActive) {
      WFLOG_WARNING("[加速] 宿主 %s 的移动组件消失，加速提前收尾。",
                    Owner ? *Owner->GetName() : TEXT("None"));
      BoostState.bBoostActive = false;
      BroadcastBoostStopped(TEXT("移动组件消失"));
    }
    ClearSprintBoostTimer();
    return;
  }

  // 用世界时间差累计，而不是 GetTimerElapsed(Handle)：后者是按 Rate 反推出来的
  // （TimerManager.cpp:795-812），掉帧补触发（CallCount > 1）时会算出负值，
  // ElapsedTime 会倒退（bug-017）。
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  const float DeltaSeconds = FMath::Max(Now - BoostState.LastUpdateTime, 0.0f);
  BoostState.LastUpdateTime = Now;

  // ===== 方向门控（只向前才加速）=====
  // 每次定时器回调都判定一次，而不是只在起手时判一次：到顶之后 `IsSpeedBoostActive()`
  // 仍是 true、速度仍被保持在目标值上，只在起手判定的话「到顶后转向侧后方」会变成
  // 无限时长的朝后加速。
  //
  // 放在累计 ElapsedTime **之前**：判定把加速收掉后进度不该继续走。
  {
    const FSprintDirectionInfo DirectionInfo = EvaluateSprintDirection();
    if (!DirectionInfo.bInputDirectionIsForward && BoostState.bBoostActive) {
      WFLOG_INFO("[加速] 方向门控结束本次加速：%s。%s",
                 *DirectionInfo.Reason, *GetSprintBoostDebugString());
      // 立刻复位（速度精确还原到基准值）。玩家重新转回前方并保持 Shift 时会重新起速。
      ResetMaxSpeed();
      return;
    }
  }

  BoostState.ElapsedTime += DeltaSeconds;

  const float Duration =
      FMath::Max(BoostState.BoostDuration, KINDA_SMALL_NUMBER);
  const float Alpha =
      FMath::Clamp(BoostState.ElapsedTime / Duration, 0.0f, 1.0f);

  // 线性插值：起始速度 -> 目标速度。想改成缓入缓出就换成 FMath::InterpEaseInOut
  const float NewMaxWalkSpeed =
      FMath::Lerp(BoostState.StartSpeed, BoostState.TargetSpeed, Alpha);

  if (Alpha >= 1.0f) {
    // 到顶：停表（不再每帧空转），速度保持到 ResetMaxSpeed 为止。
    // 注意这里只清定时器，bBoostActive 要留着，否则 IsSpeedBoostActive() 会提前变 false。
    ApplyMaxWalkSpeed(BoostState.TargetSpeed, 1.0f);
    ClearSprintBoostTimer();
    WFLOG_INFO("[加速] 已到达目标速度 %.2f（用时 %.3fs，实际经过 %.3fs），"
               "保持到 ResetMaxSpeed。%s",
               BoostState.TargetSpeed, BoostState.BoostDuration,
               BoostState.ElapsedTime, *GetSprintBoostDebugString());
    return;
  }

  ApplyMaxWalkSpeed(NewMaxWalkSpeed, Alpha);

  // 这张蓝图广播之后状态可能已经被改过（重置 / 重开一代），确认仍然有效才续挂，
  // 否则会出现两条定时器链同时跑（旧链的句柄被覆盖，谁也停不掉它）。
  if (BoostState.bBoostActive && !BoostState.bIntervalTimer &&
      BoostState.Generation == MyGeneration) {
    // 每帧模式：重新挂一次 next-tick 定时器，下一次引擎 tick 再进来。
    // 绝不能改成 SetTimer(..., 0.f, true)——InRate <= 0 在引擎里是「清掉定时器」的意思，
    // 结果是一次都不会触发（见 bug-017）。
    BoostState.TimerHandle =
        GetWorld()->GetTimerManager().SetTimerForNextTick(
            this, &USprintBoostComponent::TickSprintBoost);
  }
}

void USprintBoostComponent::TickLocalBoostVisualOnly() {
  // 本地驱动：客户端也按自己的时钟推进进度（只广播，不碰移动组件）。
  // 权威端不走这里——它由 TickSprintBoost 推进，避免两条曲线互相覆盖。
  if (IsAuthoritativeForActorComponent(this) || !BoostState.bBoostActive) {
    return;
  }

  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  const float DeltaSeconds = FMath::Max(Now - BoostState.LastUpdateTime, 0.0f);
  BoostState.LastUpdateTime = Now;

  // 客户端本地也跑一遍方向门控：本地预测与服务器结论保持一致，
  // 免得服务器已经把加速收掉了、本地进度条还在涨（等复制下来才发现）。
  // 权威结论始终在服务器：这里的本地收尾只是表现层，不写任何权威状态。
  {
    const FSprintDirectionInfo DirectionInfo = EvaluateSprintDirection();
    if (!DirectionInfo.bInputDirectionIsForward) {
      WFLOG_INFO("[加速] 本地驱动：方向门控结束本地加速表现（%s）。",
                 *DirectionInfo.Reason);
      // 本地驱动模式下不挂定时器，清状态即可（BoostAlpha 归 0，UI 立刻归位）
      BoostState.bBoostActive = false;
      BoostState.ElapsedTime = 0.0f;
      BoostAlpha = 0.0f;
      BroadcastBoostStopped(TEXT("本地方向门控"));
      OnSprintBoostUpdated.Broadcast(0.0f);
      return;
    }
  }

  BoostState.ElapsedTime += DeltaSeconds;

  const float Duration =
      FMath::Max(BoostState.BoostDuration, KINDA_SMALL_NUMBER);
  BoostAlpha = FMath::Clamp(BoostState.ElapsedTime / Duration, 0.0f, 1.0f);
  OnSprintBoostUpdated.Broadcast(BoostAlpha);
}

void USprintBoostComponent::BroadcastBoostStarted() {
  if (BoostState.bStartBroadcastPending) {
    return;
  }
  BoostState.bStartBroadcastPending = true;
  OnSprintBoostStarted.Broadcast();
}

void USprintBoostComponent::BroadcastBoostStopped(const TCHAR *Reason) {
  // 成对保护：没发过 Started 就不发 Stopped（订阅者不会收到「无对应的结束」）
  if (!BoostState.bStartBroadcastPending) {
    return;
  }
  BoostState.bStartBroadcastPending = false;
  WFLOG_INFO("[加速] 加速结束：%s。%s", Reason, *GetSprintBoostDebugString());
  OnSprintBoostStopped.Broadcast();
}

bool USprintBoostComponent::StartSpeedBoost_Implementation(
    float HoldThreshold, float TargetMaxSpeed, float BoostDuration,
    float BoostInterval) {
  WF_COMPONENT_AUTHORITY_GUARD(false);

  AActor *Owner = GetOwner();
  const FString Who = Owner ? Owner->GetName() : TEXT("None");

  WFLOG_INFO("[加速] StartSpeedBoost 进入（宿主 %s）：长按阈值 %.2fs，目标 %.2f，"
             "时长 %.2f，间隔 %.3f。%s",
             *Who, HoldThreshold, TargetMaxSpeed, BoostDuration, BoostInterval,
             *GetSprintBoostDebugString());

  // 总开关：眩晕 / 缴械 / 死亡等状态把它关掉后，起手一律不生效。
  // 已经在跑的加速也要一起收尾——只挡新请求的话，「加速中被眩晕」会一直冲下去。
  if (!bCanSprint) {
    if (BoostState.bBoostActive) {
      WFLOG_WARNING("[加速] bCanSprint=false 且正在加速中，立刻复位到基准速度。"
                    "宿主 %s",
                    *Who);
      ResetMaxSpeed();
    }
    WFLOG_WARNING("[加速] 被拒：bCanSprint=false（总开关关着）。宿主 %s", *Who);
    return false;
  }

  // HoldThreshold 是调用方的节奏参数（先按住这么久才算「长按」），
  // 不参与速度曲线计算，这里只做记录与合法性检查。
  if (!FMath::IsFinite(HoldThreshold) || HoldThreshold < 0.0f) {
    WFLOG_WARNING("[加速] 被拒：长按阈值 %.3f 非法。宿主 %s", HoldThreshold,
                  *Who);
    return false;
  }
  if (!FMath::IsFinite(TargetMaxSpeed) || TargetMaxSpeed <= 0.0f) {
    WFLOG_WARNING("[加速] 被拒：目标速度 %.2f 非法（必须 > 0）。宿主 %s",
                  TargetMaxSpeed, *Who);
    return false;
  }
  // 目标速度必须真的比基准速度高，否则「加速」只会把角色改慢
  if (TargetMaxSpeed <= BoostState.BaseMaxWalkSpeed) {
    WFLOG_WARNING("[加速] 被拒：目标速度 %.2f 不大于基准速度 %.2f。宿主 %s",
                  TargetMaxSpeed, BoostState.BaseMaxWalkSpeed, *Who);
    return false;
  }
  if (TargetMaxSpeed < MinTargetSpeed) {
    WFLOG_WARNING("[加速] 被拒：目标速度 %.2f 低于配置下限 %.2f。宿主 %s",
                  TargetMaxSpeed, MinTargetSpeed, *Who);
    return false;
  }
  if (!FMath::IsFinite(BoostDuration)) {
    // ⚠️ 负数 = 「用组件的默认值」，是**合法**输入（蓝图 pin 默认常就是 -1），
    //    绝不能在这里拒掉；只有非有限值（NaN / Inf）才是错误。
    //    ⚠️ 这里曾经写成 `BoostDuration < 0.0f` 就拒绝，结果蓝图默认的 -1.0 让
    //    每一次加速请求都被拒（本项目实测 24/24 全灭），而下方早就写好了
    //    `BoostDuration < 0 ? DefaultBoostDuration` 的回退——校验与实现互相矛盾。
    //    见 bug-028：**校验规则必须与实现接受的范围一致**。
    WFLOG_WARNING("[加速] 被拒：加速时长 %.3f 非有限值（负数表示用组件默认值，"
                  "是合法的，不要在这里拒）。宿主 %s",
                  BoostDuration, *Who);
    return false;
  }
  if (!FMath::IsFinite(BoostInterval)) {
    // 同上：负数 = 用 DefaultBoostInterval（0 = 每帧 next-tick 链也由它决定）
    WFLOG_WARNING("[加速] 被拒：刷新间隔 %.3f 非有限值（负数表示用组件默认值，"
                  "是合法的）。宿主 %s",
                  BoostInterval, *Who);
    return false;
  }

  if (IsSpeedBoostActive()) {
    // 加速中重复触发（例如蓝图每帧调一次）：保留已经在跑的这一次。
    // 不重开是有意的——重开会把速度拉回起始值，冲刺会一顿一顿的。
    WFLOG_WARNING("[加速] 被拒：已在加速中（目标 %.2f，进度 %.2f）。宿主 %s",
                  BoostState.TargetSpeed, GetSpeedBoostAlpha(), *Who);
    return false;
  }

  UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  if (Movement == nullptr) {
    WFLOG_WARNING("[加速] 被拒：宿主 %s 上没有 CharacterMovementComponent。",
                  *Who);
    return false;
  }

  // 未显式传参（< 0）时回落到组件默认值
  const float UseDuration =
      (BoostDuration >= 0.0f) ? BoostDuration : DefaultBoostDuration;
  const float UseInterval =
      (BoostInterval >= 0.0f) ? BoostInterval : DefaultBoostInterval;

  // 先还原基准速度，再抓一次基准：避免上一轮加速后的值被当成基准值
  ResetMaxSpeed();
  CaptureBaseMaxSpeed();

  BoostState.StartSpeed = Movement->MaxWalkSpeed;
  BoostState.TargetSpeed = TargetMaxSpeed;
  BoostState.BoostDuration = FMath::Max(UseDuration, KINDA_SMALL_NUMBER);
  BoostState.ElapsedTime = 0.0f;
  BoostState.bBoostActive = true;
  BoostAlpha = 0.0f;
  LastTargetSpeed = TargetMaxSpeed;
  // 代次自增：上一轮遗留的 next-tick 回调即使还在路上，也不会再续挂定时器
  ++BoostState.Generation;

  const UWorld *World = GetWorld();
  BoostState.LastUpdateTime = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;

  // 立刻推一次起始速度，让 UI / 特效不必等下一帧（ElapsedTime 还是 0，所以就是起始速度）
  ApplyMaxWalkSpeed(BoostState.StartSpeed, 0.0f);

  // 曲线要在极短时长内「瞬间到顶」时，上面那次推起始速度之后就不需要挂定时器了：
  // 直接补一次目标速度，避免多等一帧。
  if (BoostState.BoostDuration <= KINDA_SMALL_NUMBER) {
    BoostState.ElapsedTime = BoostState.BoostDuration;
    ApplyMaxWalkSpeed(BoostState.TargetSpeed, 1.0f);
    BroadcastBoostStarted();
    WFLOG_INFO("[加速] 时长配置为 0，已瞬时到顶 %.2f。%s",
               BoostState.TargetSpeed, *GetSprintBoostDebugString());
    // 可选表现照旧同步（蒙太奇为空时是一次安全空操作）
    Multicast_PlaySprintMontage(NAME_None);
    Multicast_PlaySprintEffects();
    return true;
  }

  if (UseInterval > 0.0f) {
    // 固定间隔：普通循环定时器
    BoostState.bIntervalTimer = true;
    World->GetTimerManager().SetTimer(BoostState.TimerHandle, this,
                                      &USprintBoostComponent::TickSprintBoost,
                                      UseInterval, true);
  } else {
    // 每帧：必须用 next-tick 定时器链，后续每帧由 TickSprintBoost 自己续挂。
    // 不能写 SetTimer(..., 0.f, /*bLoop=*/true)：引擎对 InRate <= 0 的处理是「清掉该句柄上的
    // 定时器」（TimerManager.h:157 的注释、TimerManager.cpp:617-659 的实现），
    // 传 0 的结果是定时器压根不存在，一次都不会触发（见 bug-017）。
    BoostState.bIntervalTimer = false;
    BoostState.TimerHandle =
        World->GetTimerManager().SetTimerForNextTick(
            this, &USprintBoostComponent::TickSprintBoost);
  }

  BroadcastBoostStarted();

  // 表现层：蒙太奇为空时 Multicast_PlaySprintMontage 内部会记一条 INFO 后跳过，
  // 所以这里可以无条件调（不需要外部再判一次）——表现层为空是设计内的合法状态。
  Multicast_PlaySprintMontage(NAME_None);
  Multicast_PlaySprintEffects();

  WFLOG_INFO("[加速] 开始：长按阈值 %.2fs，速度 %.2f -> %.2f"
             "（基准 %.2f / 蹲伏 %.2f，比例 %.3f），用时 %.3fs，"
             "刷新间隔 %.3fs（0 表示每帧）。%s",
             HoldThreshold, BoostState.StartSpeed, BoostState.TargetSpeed,
             BoostState.BaseMaxWalkSpeed,
             BoostState.BaseMaxWalkSpeedCrouched,
             BoostState.CrouchSpeedRatio, BoostState.BoostDuration, UseInterval,
             *GetSprintBoostDebugString());
  return true;
}

void USprintBoostComponent::ResetMaxSpeed_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  const bool bWasActive = BoostState.bBoostActive;

  // 未加速时调用是安全的空操作（Server_StopSpeedBoost 可以被重复发）
  ClearSprintBoostTimer();
  BoostState.bBoostActive = false;
  BoostAlpha = 0.0f;

  AActor *Owner = GetOwner();
  UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  if (Movement == nullptr) {
    WFLOG_WARNING("[加速] ResetMaxSpeed：宿主 %s 上没有移动组件，只清了定时器。",
                  Owner ? *Owner->GetName() : TEXT("None"));
    BroadcastBoostStopped(TEXT("复位（无移动组件）"));
    return;
  }

  // ⚠️ 只有「本次确实有加速在跑」时才允许写速度。
  //
  // 为什么不能无条件还原：`BoostState.BaseMaxWalkSpeed` 在**第一次加速开始之前一直是
  // 0.0**（BeginPlay 刻意不抓基准速度，见其注释；只有 StartSpeedBoost 会调
  // CaptureBaseMaxSpeed）。无条件写回就等于把角色的 `MaxWalkSpeed` 直接钉成 0 ——
  // 角色再也走不动，而且这个 0 会由 CharacterMovement 复制到拥有者客户端。
  // 本项目真实踩过：`APlayerCharacter` 在**每次攻击 / 翻滚**落锁时都会调一次本函数
  // （用于「禁止移动期间复位加速」），于是第一次攻击就把双方角色的速度钉成 0（bug-027）。
  //
  // 两个条件都必须满足：`bWasActive` 保证「确实有加速后的速度需要还原」，
  // `bBaseSpeedValid` 保证「写回去的那个基准值是可信的、不是未初始化时的 0」。
  if (!bWasActive || !BoostState.bBaseSpeedValid) {
    // 未加速时调用是安全的空操作（Server_StopSpeedBoost 可以被重复发）。
    // 不动速度：既没有「加速后的速度」需要还原，也没有可信的基准值可写。
    BoostState.StartSpeed = 0.0f;
    BoostState.TargetSpeed = 0.0f;
    BoostState.ElapsedTime = 0.0f;
    OnSprintBoostUpdated.Broadcast(0.0f);
    BroadcastBoostStopped(TEXT("空操作（当时没有加速在跑）"));
    WFLOG_INFO("[加速] ResetMaxSpeed 空操作：当时没有加速在跑，**不动** MaxWalkSpeed"
               "（保持 %.2f；基准值尚未捕获，写回会把它钉成 0）。宿主 %s",
               Movement->MaxWalkSpeed,
               Owner ? *Owner->GetName() : TEXT("None"));
    return;
  }

  // 两个基准值都精确还原（不走 ApplyMaxWalkSpeed 的比例乘法，避免浮点误差累积）
  const float SpeedBeforeReset = Movement->MaxWalkSpeed;
  const float TargetBeforeReset = BoostState.TargetSpeed;

  Movement->MaxWalkSpeed = BoostState.BaseMaxWalkSpeed;
  Movement->MaxWalkSpeedCrouched = BoostState.BaseMaxWalkSpeedCrouched;

  BoostState.StartSpeed = BoostState.BaseMaxWalkSpeed;
  BoostState.TargetSpeed = BoostState.BaseMaxWalkSpeed;
  BoostState.ElapsedTime = 0.0f;

  OnSprintBoostUpdated.Broadcast(0.0f);
  BroadcastBoostStopped(TEXT("复位到基准速度"));

  WFLOG_INFO("[加速] 已还原基准速度 %.2f -> %.2f（蹲伏 %.2f，本轮的加速目标曾为 %.2f）。宿主 %s",
             SpeedBeforeReset, BoostState.BaseMaxWalkSpeed,
             BoostState.BaseMaxWalkSpeedCrouched, TargetBeforeReset,
             Owner ? *Owner->GetName() : TEXT("None"));
}

void USprintBoostComponent::SetBaseMaxSpeed_Implementation(
    float InBaseMaxWalkSpeed, float InBaseMaxWalkSpeedCrouched) {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");

  if (!FMath::IsFinite(InBaseMaxWalkSpeed) || InBaseMaxWalkSpeed <= 0.0f) {
    WFLOG_WARNING("[加速] SetBaseMaxSpeed 被拒：基准速度 %.2f 非法。宿主 %s",
                  InBaseMaxWalkSpeed, *Who);
    return;
  }
  if (InBaseMaxWalkSpeedCrouched >= 0.0f &&
      !FMath::IsFinite(InBaseMaxWalkSpeedCrouched)) {
    WFLOG_WARNING("[加速] SetBaseMaxSpeed 被拒：蹲伏基准速度 %.2f 非法。宿主 %s",
                  InBaseMaxWalkSpeedCrouched, *Who);
    return;
  }

  if (IsSpeedBoostActive()) {
    // 加速中改写基准值会污染 ResetMaxSpeed 的还原目标，先拒绝
    WFLOG_WARNING("[加速] SetBaseMaxSpeed 被拒：加速进行中（当前基准 %.2f）。"
                  "宿主 %s",
                  BoostState.BaseMaxWalkSpeed, *Who);
    return;
  }

  const float OldBase = BoostState.BaseMaxWalkSpeed;
  const float OldCrouchBase = BoostState.BaseMaxWalkSpeedCrouched;

  BoostState.BaseMaxWalkSpeed = InBaseMaxWalkSpeed;
  if (InBaseMaxWalkSpeedCrouched >= 0.0f) {
    // 显式给了蹲伏基准：顺便更新比例，之后的加速都按新比例缩放蹲伏速度
    BoostState.BaseMaxWalkSpeedCrouched = InBaseMaxWalkSpeedCrouched;
    BoostState.CrouchSpeedRatio =
        InBaseMaxWalkSpeedCrouched / InBaseMaxWalkSpeed;
  } else {
    // 只改步行基准：蹲伏基准按原有比例推算，保持「蹲伏比步行慢」的相对关系
    BoostState.BaseMaxWalkSpeedCrouched =
        InBaseMaxWalkSpeed * BoostState.CrouchSpeedRatio;
  }

  ApplyMaxWalkSpeed(InBaseMaxWalkSpeed, 0.0f);

  WFLOG_INFO("[加速] 基准速度已改写：%.2f->%.2f（蹲伏 %.2f->%.2f，比例 %.3f）。"
             "宿主 %s",
             OldBase, BoostState.BaseMaxWalkSpeed, OldCrouchBase,
             BoostState.BaseMaxWalkSpeedCrouched, BoostState.CrouchSpeedRatio,
             *Who);
}

float USprintBoostComponent::GetSpeedBoostAlpha() const {
  if (!BoostState.bBoostActive) {
    return 0.0f;
  }

  const float Duration =
      FMath::Max(BoostState.BoostDuration, KINDA_SMALL_NUMBER);
  return FMath::Clamp(BoostState.ElapsedTime / Duration, 0.0f, 1.0f);
}

// ===== 表现同步 =====

void USprintBoostComponent::Multicast_PlaySprintMontage_Implementation(
    FName InSectionName) {
  // Multicast 会被复制到所有端，客户端理论上也能反过来调用它（发包合法）。
  // 这里加门禁：只允许服务器发起表现同步，避免客户端拿它刷屏。
  if (!IsAuthoritativeForActorComponent(this)) {
    WFLOG_ERROR("[加速] Multicast_PlaySprintMontage 被非权威端调用，已忽略。");
    return;
  }

  if (SprintMontage == nullptr) {
    // 空配置是**设计内的合法状态**（内容仓库里当前没有冲刺蒙太奇），
    // 所以这里用 INFO 而不是 WARNING，避免每次冲刺都刷一条告警。
    WFLOG_INFO("[加速] 未配置 SprintMontage，跳过冲刺蒙太奇播放（速度曲线不受影响）。"
               "宿主 %s",
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return;
  }

  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  USkeletalMeshComponent *Mesh = OwnerChar ? OwnerChar->GetMesh() : nullptr;
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    WFLOG_WARNING("[加速] 播放冲刺蒙太奇失败：宿主 %s 没有 AnimInstance。",
                  OwnerChar ? *OwnerChar->GetName() : TEXT("None"));
    return;
  }

  // 这里**不订阅 OnMontageEnded**：加速的结束条件是速度曲线 / 玩家松键，
  // 不是动画播完。动画只是表现，播完了不该反过来影响加速状态。
  const float PlayLength =
      AnimInst->Montage_Play(SprintMontage, FMath::Max(SprintMontagePlayRate,
                                                       KINDA_SMALL_NUMBER));
  if (PlayLength <= 0.0f) {
    WFLOG_WARNING("[加速] 冲刺蒙太奇 %s 播放失败（返回 %.2f，宿主 %s）。",
                  *SprintMontage->GetName(), PlayLength,
                  OwnerChar ? *OwnerChar->GetName() : TEXT("None"));
    return;
  }

  if (!InSectionName.IsNone()) {
    AnimInst->Montage_JumpToSection(InSectionName, SprintMontage);
  }

  WFLOG_INFO("[加速] 已播放冲刺蒙太奇 %s（时长 %.2fs，速率 %.2f，Section=%s）。"
             "宿主 %s",
             *SprintMontage->GetName(), PlayLength, SprintMontagePlayRate,
             *InSectionName.ToString(),
             OwnerChar ? *OwnerChar->GetName() : TEXT("None"));
}

void USprintBoostComponent::Multicast_PlaySprintEffects_Implementation() {
  // 与蒙太奇同理：只允许服务器发起表现同步
  if (!IsAuthoritativeForActorComponent(this)) {
    WFLOG_ERROR("[加速] Multicast_PlaySprintEffects 被非权威端调用，已忽略。");
    return;
  }

  WFLOG_INFO("[加速] 广播冲刺表现（PlaySprintEffects 蓝图事件）。宿主 %s",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  PlaySprintEffects();
}

void USprintBoostComponent::PlaySprintEffects_Implementation() {
  // 本地表现入口：蓝图覆写本事件即可（FOV 变化 / 粒子 / 音效 / 镜头震动）。
  // C++ 默认什么都不做。
}

// ===== 客户端 -> 服务器：加速请求 =====

bool USprintBoostComponent::Server_StartSpeedBoost_Validate(
    float HoldThreshold, float TargetMaxSpeed, float BoostDuration,
    float BoostInterval) {
  // 只做廉价检查；「目标速度是否高于基准速度」这类需要本地状态的判断在权威函数里做。
  //
  // ⚠️ BoostDuration / BoostInterval 允许**任意负值**（= 用组件默认值），所以这里的
  //    上界检查必须用「>=0 才检查上界」的写法，不能写成 `X >= -1.0f && X <= 60.0f`
  //    ——那样会把蓝图中更负的默认值（例如 -1 以外的哨兵值）判成非法。
  //    这与权威函数内的校验必须一致：两处范围不一致时，蓝图会收到「通过了 _Validate
  //    却被实现拒掉」这种最难查的组合（见 bug-028）。
  const bool bDurationValid =
      FMath::IsFinite(BoostDuration) && (BoostDuration < 0.0f || BoostDuration <= 60.0f);
  const bool bIntervalValid =
      FMath::IsFinite(BoostInterval) && (BoostInterval < 0.0f || BoostInterval <= 1.0f);

  return FMath::IsFinite(HoldThreshold) && HoldThreshold >= 0.0f &&
         HoldThreshold <= 10.0f && FMath::IsFinite(TargetMaxSpeed) &&
         TargetMaxSpeed > 0.0f && TargetMaxSpeed <= 10000.0f && bDurationValid &&
         bIntervalValid;
}

void USprintBoostComponent::Server_StartSpeedBoost_Implementation(
    float HoldThreshold, float TargetMaxSpeed, float BoostDuration,
    float BoostInterval) {
  // 服务器（含单机 / 监听服务器）上 Server RPC 就地同步执行，
  // 所以单机与联机走的是同一条路径，不需要写 HasAuthority() 分支。
  WFLOG_INFO("[加速] RPC Server_StartSpeedBoost 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  StartSpeedBoost(HoldThreshold, TargetMaxSpeed, BoostDuration, BoostInterval);
}

bool USprintBoostComponent::Server_StopSpeedBoost_Validate() { return true; }

void USprintBoostComponent::Server_StopSpeedBoost_Implementation() {
  WFLOG_INFO("[加速] RPC Server_StopSpeedBoost 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  ResetMaxSpeed();
}
