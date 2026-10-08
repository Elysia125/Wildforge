// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/CrawlingComponent.h"

#include "Animation/AnimInstance.h"
#include "Character/Components/SprintBoostComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/Settings/CrawlingComponentSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Utils/ComponentAuthorityGuard.h"
#include "Utils/WildforgeLog.h"

// 趴下的生命周期（读懂这段再改）：
//
//   客户端按键 → Server_EnterProne / Server_ExitProne / Server_ToggleProne（RPC）
//     → 服务器 EnterProne() / ExitProne()：判定通过后
//          · 选定动作类型（站立足 / 奔跑足 / 起立，**只在这里选一次**）
//          · 写玩法状态（bIsProne / bCrawlTransitionActive / 时间戳 / 累计数）
//          · Multicast_PlayCrawlMontage(动作类型)
//     → Multicast 在**每个端**本地执行一次：
//          · 各自 Montage_Play（趴下动画能被别人看到，靠的就是这一步：
//            引擎默认不复制蒙太奇播放）
//          · 播起来了 → bCrawlPresentationActive = true + 广播 OnCrawlTransitionStarted
//            + 挂本端的兜底定时器
//          · 没播起来 → 不广播、不落锁；**权威端就地收尾**
//            （否则 bCrawlTransitionActive 会永远挂着，角色再也发不起下一次）
//     → 服务器改速度（ApplyProneSpeed）—— 必须在 Multicast **之后**，见其注释
//     → 每端各自的蒙太奇 BlendOut / Ended → FinishCrawlTransition()
//          · bCrawlPresentationActive = false + 广播 OnCrawlTransitionFinished
//          · 权威端复位 bCrawlTransitionActive；如果是**起立**过渡，
//            同时把 bIsProne 置 false 并还原基准速度
//
// 关键：Started / Finished 的判据是**本端的表现标志** bCrawlPresentationActive，
// 而不是复制的 bCrawlTransitionActive —— 后者是 COND_OwnerOnly，模拟代理收不到；
// 而移动锁是每端本地的，必须每端都能就地落锁 / 解锁。
//
// 与翻滚最大的不同：趴下是**持续姿态**（bIsProne），过渡动作只是几次动画。
// 所以「姿态」与「正在过渡」是两个标志，谁也不兼任谁（合并的代价见 bug-030）。

namespace {

// 过渡类型 -> 日志里可读的名字（日志/调试串统一走它，避免各写一份导致漂移）
const TCHAR *CrawlTransitionToString(ECrawlTransition Transition) {
  switch (Transition) {
  case ECrawlTransition::EnterProneFromStand:
    return TEXT("趴下(站立起手)");
  case ECrawlTransition::EnterProneFromRun:
    return TEXT("趴下(奔跑起手)");
  case ECrawlTransition::ExitProneToStand:
    return TEXT("回滚站立");
  case ECrawlTransition::None:
  default:
    return TEXT("无");
  }
}

} // namespace

UCrawlingComponent::UCrawlingComponent() {
  // 趴下是事件驱动（RPC → 播动画 → 蒙太奇回调 / 兜底定时器），不需要每帧 Tick
  PrimaryComponentTick.bCanEverTick = false;

  // 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）。
  // 缺了这行 DOREPLIFETIME_* 等于没写——bIsProne 到不了客户端，
  // 而各端的动画蓝图都靠它保持趴下姿态。
  SetIsReplicatedByDefault(true);
}

void UCrawlingComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 姿态：**不带 condition**（= COND_None，发给所有人）。
  //
  // 规则是先问「这个值最终是谁消费的」（见 bug-030 的教训）：
  //   * 拥有者客户端的 UI → COND_OwnerOnly；
  //   * 其他玩家的表现   → COND_SimulatedOnly；
  //   * 两端都要         → 不带 condition。
  // 趴下姿态属于第三类：过渡蒙太奇播完之后，**每个端**的动画蓝图都要靠它把角色
  // 保持在趴下姿态（否则 BlendOut 一结束就弹回站立）。它是 1 个 bit，代价可忽略。
  DOREPLIFETIME(UCrawlingComponent, bIsProne);

  // 下面这几个是「服务器写、客户端只读」的状态，消费者是拥有者客户端的 UI / 输入门控
  // 与排查日志，所以用 COND_OwnerOnly。
  //
  // ⚠️ 用 COND_OwnerOnly 而不是 COND_SimulatedOnly：后者**不会**发给自主代理
  // （AutonomousProxy，也就是本地玩家自己），恰好是最需要这些值的那个端。
  DOREPLIFETIME_CONDITION(UCrawlingComponent, bCrawlTransitionActive,
                          COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, LastCrawlTransitionTime,
                          COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, CrawlCooldown, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, CrawlTransitionCount,
                          COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, ActiveCrawlMontage,
                          COND_OwnerOnly);

  // ===== 下面是「调参项」：不随游戏进程变化，只在出生束里发一次 =====
  //
  // 权威来源是 UCrawlingComponentSettings（ini）；没有 ini 覆盖时就是类默认值，
  // 总之在 BeginPlay 就定下来、整个生命周期不变，用 COND_InitialOnly 最省流量。
  //
  // ⚠️ 不用 COND_OwnerOnly：过渡判定的阈值、蒙太奇播速在**每个端**的表现逻辑里都要读，
  // 只发给 owner 会让旁观者用错值。
  // （`CrawlCooldown` 不在下面：它原本就有复制通道，沿用上面的 COND_OwnerOnly
  //  —— 它的消费者只有拥有者客户端的 UI。）
  DOREPLIFETIME_CONDITION(UCrawlingComponent, ProneMaxWalkSpeed,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, RunTransitionSpeedThreshold,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, MontagePlayRate,
                          COND_InitialOnly);
  DOREPLIFETIME_CONDITION(UCrawlingComponent, bFinishOnBlendOut,
                          COND_InitialOnly);
}

void UCrawlingComponent::ApplyGameplaySettingsOverrides() {
  // 只有权威端读 ini：客户端的这份值靠 `COND_InitialOnly` 属性复制拿到。
  if (!IsAuthoritativeForActorComponent(this)) {
    return;
  }

  const UCrawlingComponentSettings *Settings =
      UCrawlingComponentSettings::Get();
  if (Settings == nullptr) {
    WFLOG_ERROR("[配置] 趴下组件取不到 UCrawlingComponentSettings（CDO 为空），本次"
                "不应用任何 ini 覆盖，全部退回类默认值。宿主 %s",
                *GetNameSafe(GetOwner()));
    return;
  }

  int32 Applied = 0;

  // 数值下限与头文件里的 meta ClampMin 保持一致（ini 是手写文本，编辑器面板的
  // Clamp 拦不住手填的值）。
  if (Settings->bOverride_ProneMaxWalkSpeed) {
    // ⚠️ 下限是 1 而不是 0：0 是「角色被钉死」的合法速度值，写进去会永久定身
    // （bug-027 的教训）。
    ProneMaxWalkSpeed = FMath::Max(1.0f, Settings->ProneMaxWalkSpeed);
    ++Applied;
  }
  if (Settings->bOverride_RunTransitionSpeedThreshold) {
    RunTransitionSpeedThreshold =
        FMath::Max(0.0f, Settings->RunTransitionSpeedThreshold);
    ++Applied;
  }
  if (Settings->bOverride_Cooldown) {
    CrawlCooldown = FMath::Max(0.0f, Settings->Cooldown);
    ++Applied;
  }
  if (Settings->bOverride_FinishOnBlendOut) {
    bFinishOnBlendOut = Settings->bFinishOnBlendOut;
    ++Applied;
  }
  if (Settings->bOverride_MontagePlayRate) {
    // 播速是除数（蒙太奇时长 / 播速算超时），必须 > 0。
    MontagePlayRate = FMath::Max(0.01f, Settings->MontagePlayRate);
    ++Applied;
  }

  const FString Source =
      (Applied > 0) ? FString::Printf(TEXT("应用了 %d 项 ini 覆盖"), Applied)
                    : FString(TEXT("没有 ini 覆盖（全部用类默认值）"));
  WFLOG_INFO("[配置] 趴下组件（宿主 %s，权威端）：%s；生效值 爬行速度=%.0f "
             "奔跑阈值=%.0f 冷却=%.2fs BlendOut提前收尾=%d 蒙太奇播速=%.2f。",
             *GetNameSafe(GetOwner()), *Source, ProneMaxWalkSpeed,
             RunTransitionSpeedThreshold, CrawlCooldown,
             bFinishOnBlendOut ? 1 : 0, MontagePlayRate);
}

void UCrawlingComponent::BeginPlay() {
  Super::BeginPlay();

  // ini 覆盖要先应用（权威端）：下面「组件就绪」的日志与之后任何一次
  // Server_EnterProne 都按最终生效值执行。
  ApplyGameplaySettingsOverrides();

  // 动画回调在这里先订阅一次，之后每次过渡只换蒙太奇、不重复 AddDynamic。
  // 订阅在**所有端**都要做：客户端也要靠 OnMontageEnded 收尾（否则客户端
  // 会一直以为自己还在过渡中、移动锁永远不解）。
  if (ACharacter *OwnerChar = Cast<ACharacter>(GetOwner())) {
    if (USkeletalMeshComponent *Mesh = OwnerChar->GetMesh()) {
      if (UAnimInstance *AnimInst = Mesh->GetAnimInstance()) {
        AnimInst->OnMontageEnded.RemoveDynamic(
            this, &UCrawlingComponent::OnCrawlMontageEnded);
        AnimInst->OnMontageEnded.AddDynamic(
            this, &UCrawlingComponent::OnCrawlMontageEnded);

        AnimInst->OnMontageBlendingOut.RemoveDynamic(
            this, &UCrawlingComponent::OnCrawlMontageBlendingOut);
        AnimInst->OnMontageBlendingOut.AddDynamic(
            this, &UCrawlingComponent::OnCrawlMontageBlendingOut);

        bMontageCallbacksBound = true;
        BoundAnimInstance = AnimInst;
        WFLOG_INFO("[趴下] BeginPlay：已订阅动画回调（宿主 %s，AnimInstance=%s）。",
                   *OwnerChar->GetName(), *AnimInst->GetClass()->GetName());
      } else {
        // 这时 AnimInstance 可能还没创建（动画蓝图初始化有先后），
        // 拿不到就在 PlayCrawlMontageInternal 里补订。
        WFLOG_WARNING("[趴下] BeginPlay：宿主 %s 的 AnimInstance 还没创建，"
                      "动画回调将在首次过渡时补订。",
                      *OwnerChar->GetName());
      }
    } else {
      WFLOG_WARNING("[趴下] BeginPlay：宿主 %s 没有 SkeletalMeshComponent，"
                    "趴下将没有任何动画表现。",
                    *OwnerChar->GetName());
    }
  } else {
    WFLOG_WARNING("[趴下] BeginPlay：宿主不是 ACharacter，趴下只能有状态、没有表现。"
                  "宿主=%s",
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  }

  WFLOG_INFO("[趴下] 组件就绪：宿主 %s，本端权威=%d，爬行速度 %.1f，冷却 %.2fs，"
             "播速 %.2f，奔跑判定阈值 %.1f，BlendOut 提前收尾=%d，总开关=%d；"
             "蒙太奇[站立=%s 奔跑=%s 起立=%s]。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
             IsAuthoritativeForActorComponent(this) ? 1 : 0, ProneMaxWalkSpeed,
             CrawlCooldown, MontagePlayRate, RunTransitionSpeedThreshold,
             bFinishOnBlendOut ? 1 : 0, bCanCrawl ? 1 : 0,
             ProneFromStandMontage ? *ProneFromStandMontage->GetName()
                                   : TEXT("None"),
             ProneFromRunMontage ? *ProneFromRunMontage->GetName()
                                 : TEXT("None(回退站立段)"),
             ProneToStandMontage ? *ProneToStandMontage->GetName()
                                 : TEXT("None"));
}

void UCrawlingComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // 清兜底定时器：回调绑在 this 上，组件销毁后触发会打到半销毁对象
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(CrawlTimeoutHandle);
  }

  // 退订动画委托：AnimInstance 的生命周期可能比组件长（组件可以被移除），
  // 不退订就会留一个指向已销毁组件的绑定。
  if (bMontageCallbacksBound) {
    if (UAnimInstance *AnimInst = BoundAnimInstance.Get()) {
      AnimInst->OnMontageEnded.RemoveDynamic(
          this, &UCrawlingComponent::OnCrawlMontageEnded);
      AnimInst->OnMontageBlendingOut.RemoveDynamic(
          this, &UCrawlingComponent::OnCrawlMontageBlendingOut);
    }
    bMontageCallbacksBound = false;
    BoundAnimInstance = nullptr;
  }

  // 趴下状态下被销毁 / 组件被移除时把基准速度还回去，否则「趴下变慢」会留在移动组件上
  // （同样的道理见 USlideComponent 的 EndPlay）。速度是权威状态，所以只有权威端能写：
  // 客户端的本地副本由复制的镜像值跟随，在这里自己写只会和镜像值打架。
  if (bProneBaseSpeedCaptured && IsAuthoritativeForActorComponent(this)) {
    WFLOG_INFO("[趴下] EndPlay：角色在趴下状态下结束（或组件被移除），"
               "把基准速度还原成 %.2f。宿主 %s",
               BaseSpeedBeforeProne,
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    RestoreBaseSpeed();
  }

  Super::EndPlay(EndPlayReason);
}

// ===== 查询 =====

FString UCrawlingComponent::GetCrawlDebugString() const {
  const AActor *Owner = GetOwner();
  const UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;

  return FString::Printf(
      TEXT("趴下=%d 过渡中=%d 表现中=%d 动作=%s 在播=%s bCanCrawl=%d "
           "冷却=%.2fs 剩余=%.2fs 累计=%d 基准=[%.1f 已抓=%d] 爬行目标=%.1f "
           "最大速度=%.1f 移动模式=%d 速度=%.1f(水平 %.1f) 本端权威=%d"),
      bIsProne ? 1 : 0, bCrawlTransitionActive ? 1 : 0,
      bCrawlPresentationActive ? 1 : 0,
      CrawlTransitionToString(LocalActiveTransition),
      ActiveCrawlMontage ? *ActiveCrawlMontage->GetName() : TEXT("None"),
      bCanCrawl ? 1 : 0, CrawlCooldown, GetCrawlCooldownRemaining(),
      CrawlTransitionCount, BaseSpeedBeforeProne,
      bProneBaseSpeedCaptured ? 1 : 0, ProneMaxWalkSpeed,
      Movement ? Movement->MaxWalkSpeed : -1.0f,
      Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1,
      Movement ? Movement->Velocity.Size() : -1.0f,
      Movement ? Movement->Velocity.Size2D() : -1.0f,
      IsAuthoritativeForActorComponent(this) ? 1 : 0);
}

bool UCrawlingComponent::IsCrawlReady() const {
  if (CrawlCooldown <= 0.0f) {
    return true;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  // LastCrawlTransitionTime 的初值是 -1000，所以「从没过渡过」时这里天然是就绪的
  return (Now - LastCrawlTransitionTime) >= CrawlCooldown;
}

float UCrawlingComponent::GetCrawlCooldownRemaining() const {
  if (CrawlCooldown <= 0.0f || !FMath::IsFinite(LastCrawlTransitionTime)) {
    return 0.0f;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  return FMath::Max(0.0f, CrawlCooldown - (Now - LastCrawlTransitionTime));
}

float UCrawlingComponent::GetHorizontalSpeed() const {
  const AActor *Owner = GetOwner();
  const UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
  return Movement ? Movement->Velocity.Size2D() : 0.0f;
}

ECrawlTransition UCrawlingComponent::SelectEnterTransition() const {
  // 没配「奔跑趴下」= 永远是站立起手（合法的配置状态，不告警）
  if (ProneFromRunMontage == nullptr) {
    return ECrawlTransition::EnterProneFromStand;
  }

  // 读服务器上的 Velocity（自主代理的速度由客户端 move 包驱动，服务器上也是真值）。
  // ⚠️ 不要用 last input vector：它只由客户端累加，服务器上恒为零向量（见 bug-026）。
  const float Speed2D = GetHorizontalSpeed();
  if (Speed2D >= RunTransitionSpeedThreshold) {
    return ECrawlTransition::EnterProneFromRun;
  }
  return ECrawlTransition::EnterProneFromStand;
}

float UCrawlingComponent::GetMontagePlayLength(
    const UAnimMontage *Montage) const {
  if (Montage == nullptr) {
    return 1.0f;
  }
  // 注意用 GetPlayLength()：UE 5.7 里直接读 SequenceLength 已被弃用，
  // 而且它还是 protected（直接访问编译不过）。
  const float Length = Montage->GetPlayLength();
  if (Length <= 0.0f) {
    return 1.0f;
  }
  // 播速越快，实际占用时间越短——兜底时长要按播速折算，否则会多等一大截
  const float Rate = FMath::Max(MontagePlayRate, KINDA_SMALL_NUMBER);
  return Length / Rate;
}

UAnimMontage *UCrawlingComponent::ResolveMontageForTransition(
    ECrawlTransition Transition) const {
  switch (Transition) {
  case ECrawlTransition::EnterProneFromRun:
    // 奔跑段留空 = 回退到站立段（静默回退：选择点已经用 INFO 说明过了）
    return ProneFromRunMontage ? ProneFromRunMontage.Get()
                               : ProneFromStandMontage.Get();
  case ECrawlTransition::ExitProneToStand:
    return ProneToStandMontage.Get();
  case ECrawlTransition::EnterProneFromStand:
  case ECrawlTransition::None:
  default:
    return ProneFromStandMontage.Get();
  }
}

FName UCrawlingComponent::ResolveSectionForTransition(
    ECrawlTransition Transition) const {
  switch (Transition) {
  case ECrawlTransition::EnterProneFromRun:
    return ProneFromRunSectionName;
  case ECrawlTransition::ExitProneToStand:
    return ProneToStandSectionName;
  case ECrawlTransition::EnterProneFromStand:
  case ECrawlTransition::None:
  default:
    return ProneFromStandSectionName;
  }
}

// ===== 速度（趴下变慢 / 起立还原）=====

USprintBoostComponent *UCrawlingComponent::FindSprintBoostComponent() const {
  const AActor *Owner = GetOwner();
  return Owner ? Owner->FindComponentByClass<USprintBoostComponent>() : nullptr;
}

void UCrawlingComponent::ApplyProneSpeed() {
  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");

  // 1) 先收掉正在跑的加速。两个理由：趴着冲刺没有意义；而且
  //    `SetBaseMaxSpeed` 在加速进行中会被拒（它怕污染 ResetMaxSpeed 的还原目标）。
  if (USprintBoostComponent *Sprint = FindSprintBoostComponent()) {
    if (Sprint->IsSpeedBoostActive()) {
      WFLOG_INFO("[趴下] 趴下前正在加速，先收掉（ResetMaxSpeed）。宿主 %s", *Who);
      Sprint->ResetMaxSpeed();
    }
  }

  const UCharacterMovementComponent *Movement =
      GetOwner() ? GetOwner()->FindComponentByClass<UCharacterMovementComponent>()
                 : nullptr;
  const float CurrentMax = Movement ? Movement->MaxWalkSpeed : 0.0f;

  // 2) 抓「趴下前的基准速度」。必须是**复位加速之后**的实际值——抓成加速中的
  //    目标速度的话，起立会把人还原成一个带加速的永久高速（bug-027 的形态）。
  //
  //    ⚠️ 配 bProneBaseSpeedCaptured 一起判：0 是「角色不能动」的合法速度值，
  //    光看数值分不出「还没抓过」与「基准真的是 0」。
  if (!bProneBaseSpeedCaptured) {
    if (FMath::IsFinite(CurrentMax) && CurrentMax > 0.0f) {
      BaseSpeedBeforeProne = CurrentMax;
      bProneBaseSpeedCaptured = true;
      WFLOG_INFO("[趴下] 已记下趴下前的基准速度 %.2f（起立时原样还原）。宿主 %s",
                 BaseSpeedBeforeProne, *Who);
    } else {
      // 读不到有效速度（没有移动组件 / 速度本身就是 0）：本次不抓基准，
      // 起立时也就不会乱写一个 0 上去（宁可不还原，也不要把角色钉死）。
      WFLOG_WARNING("[趴下] 读不到有效的当前最大速度（%.2f），本次不记基准速度："
                    "起立时不会还原速度（避免把角色钉死在 0，见 bug-027）。宿主 %s",
                    CurrentMax, *Who);
    }
  }

  // 3) 写入爬行速度
  ApplyBaseMaxWalkSpeed(ProneMaxWalkSpeed, TEXT("进入趴下"));
}

void UCrawlingComponent::RestoreBaseSpeed() {
  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");

  // 没抓过基准（没趴下过 / 之前已经还原了）：**绝不写速度**。
  // 写回一个未初始化的 0 会把角色永久钉在原地（bug-027）。
  if (!bProneBaseSpeedCaptured) {
    WFLOG_INFO("[趴下] 没有可还原的基准速度（本次没有改写过速度），跳过速度还原。"
               "宿主 %s",
               *Who);
    return;
  }

  const float RestoreSpeed = BaseSpeedBeforeProne;
  if (ApplyBaseMaxWalkSpeed(RestoreSpeed, TEXT("起立还原"))) {
    bProneBaseSpeedCaptured = false;
    BaseSpeedBeforeProne = 0.0f;
  } else {
    // 还原失败：保留基准值，等下一次（EndPlay / 强制结束）再试，
    // 免得把一个失败的还原当成成功而把基准丢掉。
    WFLOG_WARNING("[趴下] 速度还原失败（目标 %.2f），保留基准值以便重试。宿主 %s",
                  RestoreSpeed, *Who);
  }
}

bool UCrawlingComponent::ApplyBaseMaxWalkSpeed(float InSpeed,
                                               const TCHAR *Reason) {
  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");

  // 兜底闸门：速度提升 / 降速永远不需要非正值，写进去只会把角色钉在原地
  // （bug-027 就是这么发生的），所以这里一律拒写并记 ERROR，让误用立刻可见。
  if (!FMath::IsFinite(InSpeed) || InSpeed <= 0.0f) {
    WFLOG_ERROR("[趴下] %s 被拒：目标速度 %.2f 非法（必须 > 0 且有限；"
                "0 会把角色钉死，见 bug-027）。宿主 %s",
                Reason, InSpeed, *Who);
    return false;
  }

  // 这个函数是速度写入的**唯一出口**，所以门禁放这里（调用链上虽然都有门禁，
  // 但多这一道能让将来的误用立刻在日志里可见，而不是静默改到某个客户端的本地副本）。
  if (!IsAuthoritativeForActorComponent(this)) {
    WFLOG_ERROR("[趴下] %s 在非权威端被调用，已忽略（速度是权威状态）。宿主 %s",
                Reason, *Who);
    return false;
  }

  // 首选：经 USprintBoostComponent 的公开 API 改写基准速度。
  // 它同时会把新速度同步进复制镜像（ReplicatedMaxWalkSpeed），
  // 于是拥有者客户端的预测也跟着一起变慢，不会橡皮筋（见 bug-031）。
  if (USprintBoostComponent *Sprint = FindSprintBoostComponent()) {
    Sprint->SetBaseMaxSpeed(InSpeed);

    const UCharacterMovementComponent *Movement =
        GetOwner()
            ? GetOwner()->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;
    WFLOG_INFO("[趴下] %s：基准速度经 SprintBoostComponent 写为 %.2f"
               "（移动组件实际 %.1f，拥有者客户端会通过镜像值跟随）。宿主 %s",
               Reason, InSpeed, Movement ? Movement->MaxWalkSpeed : -1.0f, *Who);
    return true;
  }

  // 降级：宿主没有加速组件（例如此组件挂到了非玩家角色上）。没有独占冲突，
  // 所以可以直接写；但 UE 5.7 的移动组件**不复制移动参数**，客户端预测不会跟随
  // （见 bug-031），这一点必须留在日志里。
  if (UCharacterMovementComponent *Movement =
          GetOwner()
              ? GetOwner()->FindComponentByClass<UCharacterMovementComponent>()
              : nullptr) {
    Movement->MaxWalkSpeed = InSpeed;
    WFLOG_INFO("[趴下] %s：宿主没有 SprintBoostComponent，直接写 "
               "MaxWalkSpeed = %.2f（⚠️ 移动参数不复制，客户端预测不会跟随，"
               "见 bug-031）。宿主 %s",
               Reason, InSpeed, *Who);
    return true;
  }

  WFLOG_WARNING("[趴下] %s 失败：宿主既没有 SprintBoostComponent 也没有 "
                "CharacterMovementComponent，速度未改变。宿主 %s",
                Reason, *Who);
  return false;
}

// ===== 权威函数 =====

bool UCrawlingComponent::EnterProne_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(false);

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[趴下] EnterProne 进入（宿主 %s）：%s", *Who,
             *GetCrawlDebugString());

  // 总开关：眩晕 / 中断等状态把它关掉后一律不生效
  if (!bCanCrawl) {
    WFLOG_WARNING("[趴下] 被拒：bCanCrawl=false（总开关关着）。宿主 %s", *Who);
    return false;
  }

  UWorld *World = GetWorld();
  if (World == nullptr) {
    WFLOG_WARNING("[趴下] 被拒：World 为空。宿主 %s", *Who);
    return false;
  }

  // 已经在趴下姿态（含正在趴下 / 正在起立）：不接受新的趴下请求。
  // 想反悔要用 ExitProne / ToggleProne，别指望再按一次趴下能取消。
  if (bIsProne) {
    WFLOG_INFO("[趴下] 被拒：已经处于趴下姿态（过渡中=%d）。要站起来请调 ExitProne / "
               "ToggleProne。宿主 %s",
               bCrawlTransitionActive ? 1 : 0, *Who);
    return false;
  }

  // 重入：上一次过渡还没收尾（理论上与 bIsProne 同时为真，这里是双保险）
  if (bCrawlTransitionActive) {
    WFLOG_WARNING("[趴下] 被拒：上一次过渡还没结束（在播 %s）。宿主 %s",
                  ActiveCrawlMontage ? *ActiveCrawlMontage->GetName()
                                     : TEXT("None"),
                  *Who);
    return false;
  }

  const float Now = World->GetTimeSeconds();

  // 冷却：用世界时间，**不要**用 GetTimerElapsed（掉帧补触发时会给出负值，见 bug-017）
  if (CrawlCooldown > 0.0f &&
      (Now - LastCrawlTransitionTime) < CrawlCooldown) {
    WFLOG_INFO("[趴下] 被拒：冷却中（距上次 %.2fs < 冷却 %.2fs，还剩 %.2fs）。"
               "宿主 %s",
               Now - LastCrawlTransitionTime, CrawlCooldown,
               CrawlCooldown - (Now - LastCrawlTransitionTime), *Who);
    return false;
  }

  // 选定这一次的过渡动作。**只在这里选一次**，之后随 Multicast 发给所有端，
  // 客户端不读任何状态自己猜该播哪一段。
  const ECrawlTransition Transition = SelectEnterTransition();
  if (Transition == ECrawlTransition::EnterProneFromStand &&
      ProneFromRunMontage == nullptr) {
    WFLOG_INFO("[趴下] 未配置 ProneFromRunMontage：本次按站立起手处理"
               "（该段是可选的，属于设计内状态）。宿主 %s",
               *Who);
  }

  // 提交玩法状态。姿态**先**置位：速度、动画蓝图、UI 都按「已经在趴下流程里」处理；
  // 即使蒙太奇没播起来，姿态也保持（与翻滚的取舍不同，见头文件）。
  bIsProne = true;
  bCrawlTransitionActive = true;
  LastCrawlTransitionTime = Now;
  ++CrawlTransitionCount;

  WFLOG_INFO("[趴下] 开始趴下：第 %d 次过渡（动作=%s，水平速度=%.1f，判定阈值=%.1f）。"
             "%s",
             CrawlTransitionCount, CrawlTransitionToString(Transition),
             GetHorizontalSpeed(), RunTransitionSpeedThreshold,
             *GetCrawlDebugString());

  // 表现同步。Multicast 会在服务器本地与所有客户端各执行一次（服务器那次是本地执行，
  // 不额外发 RPC），所以这里**不需要**再单独调一次播放——那会让同一段蒙太奇被播两次。
  //
  // ⚠️ 顺序：**先 Multicast（= 各端广播 OnCrawlTransitionStarted，角色类据此落软锁、
  // 复位加速）再改速度**。与滑行同一条教训：反过来会把「加速期间的旧速度」当成
  // 趴下前的基准抓下来，起立时再写回去 = 角色永久带着加速速度（bug-027 的形态）。
  Multicast_PlayCrawlMontage(Transition);

  // 速度：趴下 = 变慢
  ApplyProneSpeed();

  if (ActiveCrawlMontage == nullptr) {
    // 没有播起来（没配蒙太奇 / 没有 AnimInstance / Montage_Play 被拒）。
    // Multicast 分支已经在权威端就地收尾了 bCrawlTransitionActive，所以这里
    // 不会留下「永远结束不了的过渡」。姿态与爬行速度**照常生效**——趴下的姿态
    // 本身就是目的，没有动画也应该趴下去（与翻滚相反的取舍）。
    WFLOG_WARNING("[趴下] 蒙太奇没播起来：本次趴下没有动画表现"
                  "（姿态与爬行速度照常生效，检查三个蒙太奇与动画蓝图）。宿主 %s",
                  *Who);
    return false;
  }

  return true;
}

bool UCrawlingComponent::ExitProne_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(false);

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[趴下] ExitProne 进入（宿主 %s）：%s", *Who,
             *GetCrawlDebugString());

  if (!bCanCrawl) {
    WFLOG_WARNING("[趴下] 被拒：bCanCrawl=false（总开关关着）。宿主 %s", *Who);
    return false;
  }

  UWorld *World = GetWorld();
  if (World == nullptr) {
    WFLOG_WARNING("[趴下] 被拒：World 为空。宿主 %s", *Who);
    return false;
  }

  if (!bIsProne) {
    WFLOG_INFO("[趴下] 被拒：本来就不在趴下姿态，无需起身。宿主 %s", *Who);
    return false;
  }

  if (bCrawlTransitionActive) {
    WFLOG_WARNING("[趴下] 被拒：上一次过渡还没结束（在播 %s）。宿主 %s",
                  ActiveCrawlMontage ? *ActiveCrawlMontage->GetName()
                                     : TEXT("None"),
                  *Who);
    return false;
  }

  const float Now = World->GetTimeSeconds();
  if (CrawlCooldown > 0.0f &&
      (Now - LastCrawlTransitionTime) < CrawlCooldown) {
    WFLOG_INFO("[趴下] 被拒：冷却中（距上次 %.2fs < 冷却 %.2fs，还剩 %.2fs）。"
               "宿主 %s",
               Now - LastCrawlTransitionTime, CrawlCooldown,
               CrawlCooldown - (Now - LastCrawlTransitionTime), *Who);
    return false;
  }

  // 起立期间 bIsProne **保持 true**：角色仍然「在地上」，速度保持爬行速度、
  // 移动锁也还在，直到这次起立过渡收尾才真正离开趴下姿态（见 FinishCrawlTransition）。
  bCrawlTransitionActive = true;
  LastCrawlTransitionTime = Now;
  ++CrawlTransitionCount;

  WFLOG_INFO("[趴下] 开始起身：第 %d 次过渡（动作=%s）。%s", CrawlTransitionCount,
             CrawlTransitionToString(ECrawlTransition::ExitProneToStand),
             *GetCrawlDebugString());

  Multicast_PlayCrawlMontage(ECrawlTransition::ExitProneToStand);

  if (ActiveCrawlMontage == nullptr) {
    // 没播起来：Multicast 分支已在权威端就地收尾，并且**把姿态复位成站立 +
    // 还原速度**了（起立没有动画 = 立刻站起来，这与「趴下没动画也照样趴下」对称）。
    WFLOG_WARNING("[趴下] 起立蒙太奇没播起来：已在权威端就地收尾，角色直接回到站立姿态"
                  "（没有动画表现）。宿主 %s",
                  *Who);
    return false;
  }

  return true;
}

void UCrawlingComponent::ToggleProne() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  // 由**服务器**决定进还是出：客户端本地那份 bIsProne 可能已经过期，
  // 让它自己判断就会出现「客户端以为要趴下、服务器已经趴着了」的重复请求。
  if (bIsProne) {
    WFLOG_INFO("[趴下] ToggleProne：当前已趴下 → 起身。宿主 %s",
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    ExitProne();
  } else {
    WFLOG_INFO("[趴下] ToggleProne：当前站立 → 趴下。宿主 %s",
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    EnterProne();
  }
}

void UCrawlingComponent::ForceEndProne_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");

  if (!bIsProne && !bCrawlTransitionActive) {
    WFLOG_INFO("[趴下] ForceEndProne：当前既没趴下也没有过渡在跑，空操作。宿主 %s",
               *Who);
    return;
  }

  WFLOG_INFO("[趴下] ForceEndProne：强制结束趴下姿态（复活 / 传送 / 被处决等打断）。"
             "宿主 %s：%s",
             *Who, *GetCrawlDebugString());

  // 不管当前是「趴着发呆」还是「某一段过渡中途」，一律直接回到站立姿态
  // （bLeaveProne=true），并按需停掉正在播的蒙太奇。
  FinishCrawlTransition(TEXT("外部强制结束"), /*bStopMontage=*/true,
                        /*bLeaveProne=*/true);
}

void UCrawlingComponent::ResetCrawlCooldown() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  LastCrawlTransitionTime = -1000.0f;
  WFLOG_INFO("[趴下] 冷却已清除（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
}

// ===== 表现同步 =====

void UCrawlingComponent::Multicast_PlayCrawlMontage_Implementation(
    ECrawlTransition Transition) {
  const bool bAuthority = IsAuthoritativeForActorComponent(this);

  // ⚠️ 本函数在**每个端**都会执行一次，而且「客户端自己调用」时也只会在本端执行
  // （引擎行为：Multicast 在客户端的 callspace 是 Local，既不发服务器也不广播给别人，
  // 见头文件里对 `AActor::GetFunctionCallspace` 的引用）。
  // 所以它必须对「任何端、任何调用来源」都安全：**只播动画 + 维护本端表现标志，
  // 不写任何玩法状态**，也**不能**加「非权威端就忽略」的门禁（那正是 bug-033 的形态：
  // 客户端收到 Multicast 时必然是非权威端，加门禁等于客户端永远看不到动画）。
  WFLOG_INFO("[趴下] Multicast_PlayCrawlMontage 到达（%s）：动作=%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             CrawlTransitionToString(Transition));

  const bool bPlayed = PlayCrawlMontageInternal(Transition);

  if (!bPlayed) {
    // 表现没起来：**一个事件都不广播**。
    // Started / Finished 必须严格配对——广播了 Started 却没有蒙太奇来触发结束，
    // 订阅者（移动门控）的锁就永远挂在那里。
    WFLOG_INFO("[趴下] Multicast 之后没有在播的蒙太奇（动作=%s）：本次过渡没有动画表现，"
               "也不会广播 OnCrawlTransitionStarted。%s",
               CrawlTransitionToString(Transition), *GetCrawlDebugString());

    if (bAuthority) {
      // 权威端此刻 bCrawlTransitionActive 已经置位，而「没有任何东西会来结束它」——
      // 必须就地收尾，否则角色再也发不起下一次趴下 / 起立。
      // 姿态按方向落：趴下没动画 = 照样趴下（姿态是目的）；起立没动画 = 直接站起来。
      const bool bLeaveProne =
          (Transition == ECrawlTransition::ExitProneToStand);
      WFLOG_WARNING("[趴下] 权威端就地收尾：本次过渡没有任何东西会来结束它，"
                    "现在复位状态以免卡住重入与冷却（动作=%s，是否离开趴下=%d）。宿主 %s",
                    CrawlTransitionToString(Transition), bLeaveProne ? 1 : 0,
                    GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
      FinishCrawlTransition(TEXT("过渡表现未能开始"), /*bStopMontage=*/false,
                            bLeaveProne);
    }
    return;
  }

  // 表现真的播起来了：置本端标志 + 挂兜底定时器 + 广播 Started。
  //
  // 顺序是有意的：**先置标志再广播**，订阅者收到 Started 后若立刻回头查询
  // `IsCrawlPresentationActive()` / `GetLocalCrawlTransition()`，读到的必须已经是本次的值。
  bCrawlPresentationActive = true;

  // 兜底定时器**每个端都挂**（与翻滚只在权威端挂不同）：每个端都有自己的一份
  // 表现状态要收尾，缺了它，某个端的移动锁就只能等蒙太奇自然结束。
  OpenCrawlTimeout();

  OnCrawlTransitionStarted.Broadcast(Transition);
  WFLOG_INFO("[趴下] %s 已广播 OnCrawlTransitionStarted（动作=%s，订阅者会落移动锁）。%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             CrawlTransitionToString(Transition), *GetCrawlDebugString());
}

void UCrawlingComponent::Multicast_PlayCrawlEffects_Implementation(
    ECrawlTransition Transition) {
  // ⚠️ 这里**不能**加「非权威端就忽略」的门禁：Multicast 在每个端都会执行，
  // 客户端收到的那一次本来就不是权威端——加了门禁等于客户端永远看不到特效
  // （见 bug-033）。表现同步要的就是「每个端各自本地播一遍」。
  WFLOG_INFO("[趴下] 广播趴下表现（PlayCrawlEffects 蓝图事件，动作=%s，本端权威=%d）。宿主 %s",
             CrawlTransitionToString(Transition),
             IsAuthoritativeForActorComponent(this) ? 1 : 0,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  PlayCrawlEffects(Transition);
}

void UCrawlingComponent::PlayCrawlEffects_Implementation(
    ECrawlTransition Transition) {
  // 本地表现入口：蓝图覆写本事件即可（尘土 / 音效 / 镜头震动）。
  // C++ 默认什么都不做。
}

void UCrawlingComponent::OpenCrawlTimeout() {
  UWorld *World = GetWorld();
  if (World == nullptr) {
    return;
  }

  // 兜底时长 = 蒙太奇时长（已按播速折算） + 1s 余量。
  // 加余量是因为 BlendOut / 混合空间可能让蒙太奇多花一点时间才真正结束；
  // 余量太小会在正常过渡时误触发强制收尾。
  const float TimeoutSeconds = GetMontagePlayLength(ActiveCrawlMontage) + 1.0f;

  FTimerDelegate TimeoutDelegate;
  TimeoutDelegate.BindUObject(this, &UCrawlingComponent::HandleCrawlTimeout);
  World->GetTimerManager().SetTimer(CrawlTimeoutHandle, TimeoutDelegate,
                                    TimeoutSeconds, /*bLoop=*/false);

  WFLOG_INFO("[趴下] 已挂兜底定时器：%.2fs 后若动画仍未结束就强制收尾"
             "（蒙太奇时长 %.2fs / 播速 %.2f + 1s 余量）。%s",
             TimeoutSeconds,
             ActiveCrawlMontage ? ActiveCrawlMontage->GetPlayLength() : -1.0f,
             MontagePlayRate, *GetCrawlDebugString());
}

void UCrawlingComponent::HandleCrawlTimeout() {
  // 判据用「本端是否在演过渡」这个表现标志：它与权威端的玩法状态同步置位、同步复位，
  // 但读它更贴合「这个定时器在守什么」（动画有没有结束）。
  if (!bCrawlPresentationActive) {
    // 正常收尾时定时器本该被清掉；走到这里说明清理路径漏了，只记一条 INFO
    WFLOG_INFO("[趴下] 兜底定时器触发，但当前并没有过渡表现在播"
               "（可能已被正常收尾）。%s",
               *GetCrawlDebugString());
    return;
  }

  WFLOG_WARNING("[趴下] 动画超时未结束，强制收尾（OnMontageEnded 没有按预期触发，"
                "检查蒙太奇是否被打断或 AnimInstance 被替换）。%s",
                *GetCrawlDebugString());

  const bool bLeaveProne =
      (LocalActiveTransition == ECrawlTransition::ExitProneToStand);
  FinishCrawlTransition(TEXT("过渡动画超时未结束"), /*bStopMontage=*/true,
                        bLeaveProne);
}

bool UCrawlingComponent::PlayCrawlMontageInternal(ECrawlTransition Transition) {
  // 无论成功失败，都先把「本端在播什么」清干净：失败时留着旧值会让
  // OnCrawlMontageEnded 的判定认错对象。
  ActiveCrawlMontage = nullptr;
  LocalActiveTransition = ECrawlTransition::None;

  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  if (OwnerChar == nullptr) {
    WFLOG_WARNING("[趴下] 播放失败：宿主 %s 不是 ACharacter。",
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return false;
  }

  UAnimMontage *Montage = ResolveMontageForTransition(Transition);
  if (Montage == nullptr) {
    WFLOG_WARNING("[趴下] 播放失败：动作 %s 对应的蒙太奇未配置（宿主 %s）。",
                  CrawlTransitionToString(Transition),
                  *OwnerChar->GetName());
    return false;
  }

  USkeletalMeshComponent *Mesh = OwnerChar->GetMesh();
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    WFLOG_WARNING("[趴下] 播放失败：宿主 %s 没有 AnimInstance（Mesh=%s）。",
                  *OwnerChar->GetName(),
                  Mesh ? *Mesh->GetName() : TEXT("None"));
    return false;
  }

  // 回调订阅：BeginPlay 时 AnimInstance 可能还没创建；另外动画蓝图被整体换掉
  // （`Mesh->SetAnimInstanceClass`）之后旧绑定不会再触发，所以这里按**实例**判断，
  // 而不是只用一个「绑过没有」的布尔（那会让换过实例之后只能等兜底定时器）。
  if (!bMontageCallbacksBound || BoundAnimInstance.Get() != AnimInst) {
    if (UAnimInstance *OldInst = BoundAnimInstance.Get()) {
      OldInst->OnMontageEnded.RemoveDynamic(
          this, &UCrawlingComponent::OnCrawlMontageEnded);
      OldInst->OnMontageBlendingOut.RemoveDynamic(
          this, &UCrawlingComponent::OnCrawlMontageBlendingOut);
    }

    AnimInst->OnMontageEnded.RemoveDynamic(
        this, &UCrawlingComponent::OnCrawlMontageEnded);
    AnimInst->OnMontageEnded.AddDynamic(
        this, &UCrawlingComponent::OnCrawlMontageEnded);
    AnimInst->OnMontageBlendingOut.RemoveDynamic(
        this, &UCrawlingComponent::OnCrawlMontageBlendingOut);
    AnimInst->OnMontageBlendingOut.AddDynamic(
        this, &UCrawlingComponent::OnCrawlMontageBlendingOut);

    bMontageCallbacksBound = true;
    BoundAnimInstance = AnimInst;
    WFLOG_INFO("[趴下] 已订阅动画回调（宿主 %s，AnimInstance=%s）。",
               *OwnerChar->GetName(), *AnimInst->GetClass()->GetName());
  }

  // 真正播放。用返回值判成败：<= 0 表示引擎拒播
  // （该蒙太奇不在当前状态机里、被 Montage_Stop 之类拒掉等）。
  const float Rate = FMath::Max(MontagePlayRate, KINDA_SMALL_NUMBER);
  const float PlayLength = AnimInst->Montage_Play(Montage, Rate);
  if (PlayLength <= 0.0f) {
    WFLOG_WARNING("[趴下] %s 播放失败（Montage_Play 返回 %.2f，播速 %.2f，宿主 %s）。",
                  *Montage->GetName(), PlayLength, Rate, *OwnerChar->GetName());
    return false;
  }

  // 跳到指定 Section（NAME_None = 留在第 0 段）
  const FName Section = ResolveSectionForTransition(Transition);
  if (!Section.IsNone()) {
    AnimInst->Montage_JumpToSection(Section, Montage);
    WFLOG_INFO("[趴下] 已跳到 Section %s。", *Section.ToString());
  }

  // 记录「本段实际在播哪个蒙太奇 / 哪次过渡」：结束回调只认它，
  // 不要用配置项去比对（配置可能被热改，而且动画蓝图可能换了实例）。
  ActiveCrawlMontage = Montage;
  LocalActiveTransition = Transition;

  WFLOG_INFO("[趴下] 已播放 %s（动作=%s，时长 %.2fs，播速 %.2f，折算后 %.2fs，"
             "Section=%s，宿主 %s）。",
             *Montage->GetName(), CrawlTransitionToString(Transition),
             Montage->GetPlayLength(), Rate, GetMontagePlayLength(Montage),
             Section.IsNone() ? TEXT("None") : *Section.ToString(),
             *OwnerChar->GetName());
  return true;
}

void UCrawlingComponent::OnCrawlMontageEnded(UAnimMontage *Montage,
                                             bool bInterrupted) {
  WFLOG_INFO("[趴下] OnMontageEnded：%s（被打断=%d）| 本组件在播=%s | 动作=%s | "
             "表现中=%d | 刚从 BlendOut 收尾=%d",
             Montage ? *Montage->GetName() : TEXT("None"), bInterrupted ? 1 : 0,
             ActiveCrawlMontage ? *ActiveCrawlMontage->GetName()
                                : TEXT("None"),
             CrawlTransitionToString(LocalActiveTransition),
             bCrawlPresentationActive ? 1 : 0, bFinishingFromBlendOut ? 1 : 0);

  // 关键判定：**只认本组件正在播的那一个蒙太奇**。
  // 引擎的 OnMontageEnded 会为「任何」蒙太奇触发（受击 / 死亡 / 翻滚…），
  // 不过滤的话会被无关动画把趴下状态清掉（移动锁也就莫名其妙地解了）。
  if (Montage == nullptr || Montage != ActiveCrawlMontage) {
    WFLOG_INFO("[趴下] 该 OnMontageEnded 不是本组件的过渡蒙太奇，忽略。");
    return;
  }

  if (bFinishingFromBlendOut) {
    // BlendOut 已经收过尾了，这里只清引用（状态不重复复位、事件不重复广播）
    WFLOG_INFO("[趴下] BlendOut 已收尾，忽略随后到来的 OnMontageEnded。");
    bFinishingFromBlendOut = false;
    ActiveCrawlMontage = nullptr;
    LocalActiveTransition = ECrawlTransition::None;
    return;
  }

  const bool bLeaveProne =
      (LocalActiveTransition == ECrawlTransition::ExitProneToStand);
  FinishCrawlTransition(bInterrupted ? TEXT("过渡蒙太奇被打断")
                                     : TEXT("过渡蒙太奇正常播完"),
                        /*bStopMontage=*/false, bLeaveProne);
}

void UCrawlingComponent::OnCrawlMontageBlendingOut(UAnimMontage *Montage,
                                                   bool bInterrupted) {
  if (!bFinishOnBlendOut) {
    return;
  }
  if (Montage == nullptr || Montage != ActiveCrawlMontage) {
    // 不是我们的蒙太奇，静默跳过（这个回调对每个蒙太奇都会触发，打日志会刷屏）
    return;
  }
  if (!bCrawlPresentationActive) {
    // 已经被超时 / 强制结束收过尾了。
    // ⚠️ 判据必须是本端表现标志，不能是复制的 bCrawlTransitionActive：
    // 它是 COND_OwnerOnly，模拟代理永远读不到 true。
    return;
  }

  WFLOG_INFO("[趴下] 进入 BlendOut（被打断=%d），bFinishOnBlendOut=true → 提前收尾，"
             "不等蒙太奇完全播完。%s",
             bInterrupted ? 1 : 0, *GetCrawlDebugString());

  const bool bLeaveProne =
      (LocalActiveTransition == ECrawlTransition::ExitProneToStand);
  bFinishingFromBlendOut = true;
  // 这里**不**主动停蒙太奇：让它自然淡出，否则过渡的收尾动作会被硬切掉。
  FinishCrawlTransition(TEXT("BlendOut 提前收尾"), /*bStopMontage=*/false,
                        bLeaveProne);
}

void UCrawlingComponent::FinishCrawlTransition(const TCHAR *Reason,
                                              bool bStopMontage,
                                              bool bLeaveProne) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(CrawlTimeoutHandle);
  }

  if (bStopMontage && ActiveCrawlMontage != nullptr) {
    if (ACharacter *OwnerChar = Cast<ACharacter>(GetOwner())) {
      if (USkeletalMeshComponent *Mesh = OwnerChar->GetMesh()) {
        if (UAnimInstance *AnimInst = Mesh->GetAnimInstance()) {
          AnimInst->Montage_Stop(/*InBlendOutTime=*/0.1f, ActiveCrawlMontage);
          WFLOG_INFO("[趴下] 已主动停掉蒙太奇 %s（0.1s 淡出）。",
                     *ActiveCrawlMontage->GetName());
        }
      }
    }
  }

  // 收尾要对两类状态分开处理：
  //   * 表现标志 bCrawlPresentationActive：**每个端**都要写，它决定要不要广播 Finished；
  //   * 玩法状态 bIsProne / bCrawlTransitionActive：**只有权威端**能写
  //     （客户端不写玩法状态：拥有者客户端的值由服务器复制下来，模拟代理则根本
  //     收不到这几个 COND_OwnerOnly 属性）。
  const bool bAuthority = IsAuthoritativeForActorComponent(this);
  const bool bWasPresenting = bCrawlPresentationActive;
  const ECrawlTransition FinishedTransition = LocalActiveTransition;
  const bool bWasProne = bIsProne;

  bCrawlPresentationActive = false;
  LocalActiveTransition = ECrawlTransition::None;
  ActiveCrawlMontage = nullptr;

  if (bAuthority) {
    bCrawlTransitionActive = false;
    if (bLeaveProne) {
      // 起立过渡收尾（或外部强制结束）：真正离开趴下姿态
      bIsProne = false;
    }
  }

  WFLOG_INFO("[趴下] 过渡结束：%s（动作=%s，原表现中=%d，原趴下=%d，是否离开趴下=%d，"
             "累计 %d 次，本端权威=%d）。%s",
             Reason, CrawlTransitionToString(FinishedTransition),
             bWasPresenting ? 1 : 0, bWasProne ? 1 : 0, bLeaveProne ? 1 : 0,
             CrawlTransitionCount, bAuthority ? 1 : 0, *GetCrawlDebugString());

  // Started / Finished 严格配对：只有本端真的广播过 Started（= bWasPresenting）
  // 才广播 Finished。不配对时宁可少广播一次，也不要让订阅者收到「无配对的解锁请求」——
  // 那会让「锁的持有者集合」与实际状态不一致，排查时反而更乱。
  if (bWasPresenting) {
    // 通知订阅者（释放移动锁、回到姿态动画等）。服务器与各客户端都会各自广播一次。
    OnCrawlTransitionFinished.Broadcast(FinishedTransition);
    WFLOG_INFO("[趴下] 已广播 OnCrawlTransitionFinished（动作=%s，订阅者会释放移动锁）。",
               CrawlTransitionToString(FinishedTransition));
  }

  // 速度还原放在**广播之后**：广播会让角色类落锁 / 复位加速，那一步是幂等的；
  // 反过来（先还原再广播）会让落锁时的加速复位把刚还原的基准值再写一遍，
  // 虽然结果相同，但日志顺序会让人误以为还原发生在落锁之前。
  if (bAuthority && bLeaveProne) {
    RestoreBaseSpeed();
  }
}

// ===== 客户端 -> 服务器 =====

bool UCrawlingComponent::Server_EnterProne_Validate() {
  // 无参数，没有可校验的输入；真正的重入 / 冷却 / 总开关判定在 EnterProne() 内部
  return true;
}

void UCrawlingComponent::Server_EnterProne_Implementation() {
  // 服务器（含单机 / 监听服务器）上 Server RPC 就地同步执行，
  // 所以单机与联机走的是同一条路径，不需要写 HasAuthority() 分支。
  WFLOG_INFO("[趴下] RPC Server_EnterProne 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  EnterProne();
}

bool UCrawlingComponent::Server_ExitProne_Validate() { return true; }

void UCrawlingComponent::Server_ExitProne_Implementation() {
  WFLOG_INFO("[趴下] RPC Server_ExitProne 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  ExitProne();
}

bool UCrawlingComponent::Server_ToggleProne_Validate() { return true; }

void UCrawlingComponent::Server_ToggleProne_Implementation() {
  WFLOG_INFO("[趴下] RPC Server_ToggleProne 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  ToggleProne();
}
