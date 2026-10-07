// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/LandRollComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Utils/ComponentAuthorityGuard.h"
#include "Utils/WildforgeLog.h"

// 翻滚的生命周期（读懂这段再改）：
//
//   客户端点按 → Server_LandRoll（RPC）
//     → 服务器 LandRoll()：判定通过后写 bIsRolling / LastLandRollTime（权威玩法状态）
//     → Multicast_PlayLandRollMontage(段名)：在**每个端**本地执行一次
//          · 各自 Montage_Play（翻滚动画能被别人看到，靠的就是这一步：
//            引擎默认不复制蒙太奇播放）
//          · 播起来了 → bRollPresentationActive = true + 广播 OnLandRollStarted
//          · 没播起来 → 不广播、不落锁；权威端就地收尾
//            （否则 bIsRolling 会永远挂着，冷却与重入判定全被卡死）
//     → 服务器挂兜底定时器（防动画不结束）
//     → 每端各自的蒙太奇 BlendOut / Ended → FinishLandRoll()
//          · bRollPresentationActive = false + 广播 OnLandRollFinished
//          · 权威端同时把 bIsRolling 复位（客户端不写玩法状态）
//
// 关键：Started / Finished 的判据是**本端的表现标志** bRollPresentationActive，
// 而不是复制的 bIsRolling —— 后者是 COND_OwnerOnly，模拟代理（其他玩家）永远收不到，
// 拿它当判据会让那些端「只落锁、没人解锁」（见头文件的说明）。
//
// 兜底：OpenRollTimeout() 在动画不结束（被打断 / AnimInstance 被换 /
// 动画蓝图出错）时强制收尾，否则该端会永远停在「表现中」。

ULandRollComponent::ULandRollComponent() {
  // 翻滚是事件驱动（RPC → 播动画 → 蒙太奇回调），不需要每帧 Tick
  PrimaryComponentTick.bCanEverTick = false;

  // 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）。
  // 缺了这行 DOREPLIFETIME_* 等于没写——bIsRolling 到不了客户端，
  // 而客户端拿不到它就做不了「翻滚中禁用输入」的门控。
  SetIsReplicatedByDefault(true);
}

void ULandRollComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 这几个都是「服务器写、客户端只读」的状态。
  //
  // ⚠️ 用 COND_OwnerOnly 而不是 COND_SimulatedOnly：后者**不会**发给自主代理
  // （AutonomousProxy，也就是本地玩家自己），恰好是最需要这些值的那个端——
  // 写成 SimulatedOnly 的话本地玩家的 UI 永远读不到 bIsRolling（这是个静默
  // bug）。
  DOREPLIFETIME_CONDITION(ULandRollComponent, bIsRolling, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, LastLandRollTime, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, LandRollCooldown, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, LandRollCount, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, ActiveLandRollMontage,
                          COND_OwnerOnly);
}

void ULandRollComponent::BeginPlay() {
  Super::BeginPlay();

  // 动画回调在这里统一订阅一次，之后每次翻滚只换蒙太奇、不重复 AddDynamic。
  // 订阅在**所有端**都要做：客户端也要靠 OnMontageEnded 收尾（否则客户端
  // 会一直以为自己还在翻滚中）。
  if (ACharacter *OwnerChar = Cast<ACharacter>(GetOwner())) {
    if (USkeletalMeshComponent *Mesh = OwnerChar->GetMesh()) {
      // AnimInstance
      // 这时可能还没创建（动画蓝图初始化有先后），所以这里拿到就绑， 拿不到就在
      // PlayLandRollMontageInternal 里补绑（BindMontageCallbacks 的等价逻辑）。
      if (UAnimInstance *AnimInst = Mesh->GetAnimInstance()) {
        AnimInst->OnMontageEnded.RemoveDynamic(
            this, &ULandRollComponent::OnLandRollMontageEnded);
        AnimInst->OnMontageEnded.AddDynamic(
            this, &ULandRollComponent::OnLandRollMontageEnded);

        AnimInst->OnMontageBlendingOut.RemoveDynamic(
            this, &ULandRollComponent::OnLandRollMontageBlendingOut);
        AnimInst->OnMontageBlendingOut.AddDynamic(
            this, &ULandRollComponent::OnLandRollMontageBlendingOut);

        bMontageCallbacksBound = true;
        WFLOG_INFO(
            "[翻滚] BeginPlay：已订阅动画回调（宿主 %s，AnimInstance=%s）。",
            *OwnerChar->GetName(), *AnimInst->GetClass()->GetName());
      } else {
        WFLOG_WARNING("[翻滚] BeginPlay：宿主 %s 的 AnimInstance 还没创建，"
                      "动画回调将在首次翻滚时补订。",
                      *OwnerChar->GetName());
      }
    } else {
      WFLOG_WARNING("[翻滚] BeginPlay：宿主 %s 没有 SkeletalMeshComponent，"
                    "翻滚将没有任何动画表现。",
                    *OwnerChar->GetName());
    }
  } else {
    WFLOG_WARNING(
        "[翻滚] BeginPlay：宿主不是 ACharacter，翻滚只能有状态、没有表现。"
        "宿主=%s",
        GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  }

  WFLOG_INFO("[翻滚] 组件就绪：宿主 %s，本端权威=%d，冷却 %.2fs，"
             "蒙太奇=%s，播速 %.2f，BlendOut 提前解锁=%d，总开关=%d。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"),
             IsAuthoritativeForActorComponent(this) ? 1 : 0, LandRollCooldown,
             LandRollMontage ? *LandRollMontage->GetName() : TEXT("None"),
             MontagePlayRate, bFinishOnBlendOut ? 1 : 0, bCanLandRoll ? 1 : 0);
}

void ULandRollComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // 清兜底定时器：回调绑在 this 上，组件销毁后触发会打到半销毁对象
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(RollTimeoutHandle);
  }

  // 退订动画委托：AnimInstance 的生命周期可能比组件长（组件可以被移除），
  // 不退订就会留一个指向已销毁组件的绑定。
  if (bMontageCallbacksBound) {
    if (ACharacter *OwnerChar = Cast<ACharacter>(GetOwner())) {
      if (USkeletalMeshComponent *Mesh = OwnerChar->GetMesh()) {
        if (UAnimInstance *AnimInst = Mesh->GetAnimInstance()) {
          AnimInst->OnMontageEnded.RemoveDynamic(
              this, &ULandRollComponent::OnLandRollMontageEnded);
          AnimInst->OnMontageBlendingOut.RemoveDynamic(
              this, &ULandRollComponent::OnLandRollMontageBlendingOut);
        }
      }
    }
    bMontageCallbacksBound = false;
  }

  Super::EndPlay(EndPlayReason);
}

// 翻滚状态的一行快照：滚不动时能直接从日志看出卡在哪一步
// （RPC 有没有到服务器 → 总开关 → 重入 → 冷却 → 蒙太奇有没有播起来）。
FString ULandRollComponent::GetLandRollDebugString() const {
  const AActor *Owner = GetOwner();
  const UCharacterMovementComponent *Movement =
      Owner ? Owner->FindComponentByClass<UCharacterMovementComponent>()
            : nullptr;

  return FString::Printf(
      TEXT("bIsRolling=%d 表现中=%d bCanLandRoll=%d 冷却=%.2fs 剩余=%.2fs 累计=%d "
           "在播=%s 蒙太奇=%s 播速=%.2f 移动模式=%d 速度=%.1f 本端权威=%d"),
      bIsRolling ? 1 : 0, bRollPresentationActive ? 1 : 0, bCanLandRoll ? 1 : 0,
      LandRollCooldown, GetLandRollCooldownRemaining(), LandRollCount,
      ActiveLandRollMontage ? *ActiveLandRollMontage->GetName() : TEXT("None"),
      LandRollMontage ? *LandRollMontage->GetName() : TEXT("None"),
      MontagePlayRate,
      Movement ? static_cast<int32>(Movement->MovementMode.GetValue()) : -1,
      Movement ? Movement->Velocity.Size() : -1.0f,
      IsAuthoritativeForActorComponent(this) ? 1 : 0);
}

bool ULandRollComponent::IsLandRollReady() const {
  if (LandRollCooldown <= 0.0f) {
    return true;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  // LastLandRollTime 的初值是 -1000，所以「从没滚过」时这里天然是就绪的
  return (Now - LastLandRollTime) >= LandRollCooldown;
}

float ULandRollComponent::GetLandRollCooldownRemaining() const {
  if (LandRollCooldown <= 0.0f || !FMath::IsFinite(LastLandRollTime)) {
    return 0.0f;
  }
  const UWorld *World = GetWorld();
  const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
  return FMath::Max(0.0f, LandRollCooldown - (Now - LastLandRollTime));
}

float ULandRollComponent::GetMontagePlayLength() const {
  const UAnimMontage *Montage = LandRollMontage;
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

void ULandRollComponent::OpenRollTimeout() {
  UWorld *World = GetWorld();
  if (World == nullptr) {
    return;
  }

  // 兜底时长 = 蒙太奇时长（已按播速折算） + 1s 余量。
  // 加余量是因为 BlendOut / 混合空间可能让蒙太奇多花一点时间才真正结束；
  // 余量太小会在正常翻滚时误触发强制收尾。
  const float TimeoutSeconds = GetMontagePlayLength() + 1.0f;

  FTimerDelegate TimeoutDelegate;
  TimeoutDelegate.BindUObject(this, &ULandRollComponent::HandleLandRollTimeout);
  World->GetTimerManager().SetTimer(RollTimeoutHandle, TimeoutDelegate,
                                    TimeoutSeconds, /*bLoop=*/false);

  WFLOG_INFO("[翻滚] 已挂兜底定时器：%.2fs 后若动画仍未结束就强制收尾"
             "（蒙太奇时长 %.2fs / 播速 %.2f + 1s 余量）。%s",
             TimeoutSeconds,
             LandRollMontage ? LandRollMontage->GetPlayLength() : -1.0f,
             MontagePlayRate, *GetLandRollDebugString());
}

void ULandRollComponent::HandleLandRollTimeout() {
  // 本定时器只在权威端挂（见 LandRoll_Implementation）。
  // 判据用「本端是否在演翻滚」这个表现标志：它与权威端的玩法状态同步置位、同步复位，
  // 但读它更贴合「这个定时器在守什么」（动画有没有结束）。
  if (!bRollPresentationActive) {
    if (bIsRolling) {
      // 表现没了、玩法状态还挂着：这正是「角色再也滚不了」的前兆
      // （bIsRolling 永久为 true → 重入判定永远拒绝新翻滚）。
      // 不能只打日志，必须把状态复位掉。
      WFLOG_WARNING("[翻滚] 兜底定时器触发：本端已无翻滚表现，但 bIsRolling 仍为 1，"
                    "强制收尾以免重入与冷却判定被永久卡住。%s",
                    *GetLandRollDebugString());
      FinishLandRoll(TEXT("表现丢失后的兜底收尾"), /*bStopMontage=*/false);
      return;
    }

    // 正常结束后定时器本该被清掉；走到这里说明清理路径漏了，只记一条 INFO
    WFLOG_INFO(
        "[翻滚] 兜底定时器触发，但当前并不在翻滚中（可能已被正常收尾）。%s",
        *GetLandRollDebugString());
    return;
  }

  WFLOG_WARNING(
      "[翻滚] 动画超时未结束，强制收尾（Animation Notify / OnMontageEnded "
      "没有按预期触发，检查蒙太奇是否被打断或 AnimInstance 被替换）。%s",
      *GetLandRollDebugString());
  FinishLandRoll(TEXT("蒙太奇超时未结束"), /*bStopMontage=*/true);
}

bool ULandRollComponent::LandRoll_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(false);

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[翻滚] LandRoll 进入（宿主 %s）：%s", *Who,
             *GetLandRollDebugString());

  // 总开关：眩晕 / 缴械 / 死亡等状态把它关掉后一律不生效
  if (!bCanLandRoll) {
    WFLOG_WARNING("[翻滚] 被拒：bCanLandRoll=false（总开关关着）。宿主 %s",
                  *Who);
    return false;
  }

  UWorld *World = GetWorld();
  if (World == nullptr) {
    WFLOG_WARNING("[翻滚] 被拒：World 为空。宿主 %s", *Who);
    return false;
  }

  // 重入：上一段翻滚还没结束就不接受新的（`bIsRolling` 只有在收尾时才置 false）
  if (bIsRolling) {
    WFLOG_WARNING("[翻滚] 被拒：已在翻滚中（在播 %s）。宿主 %s",
                  ActiveLandRollMontage ? *ActiveLandRollMontage->GetName()
                                        : TEXT("None"),
                  *Who);
    return false;
  }

  const float Now = World->GetTimeSeconds();

  // 冷却：用世界时间，**不要**用 GetTimerElapsed（掉帧补触发时会给出负值，见
  // bug-017）
  if (LandRollCooldown > 0.0f && (Now - LastLandRollTime) < LandRollCooldown) {
    WFLOG_INFO("[翻滚] 被拒：冷却中（距上次 %.2fs < 冷却 %.2fs，还剩 %.2fs）。"
               "宿主 %s",
               Now - LastLandRollTime, LandRollCooldown,
               LandRollCooldown - (Now - LastLandRollTime), *Who);
    return false;
  }

  // 蒙太奇没配：仍然推进状态（翻滚的位移可能完全由蓝图驱动），但要明确告警——
  // 「滚了但没动画」在联机排查时很难和「动画没同步」区分开。
  if (LandRollMontage == nullptr) {
    WFLOG_WARNING("[翻滚] 未配置 LandRollMontage：本次翻滚没有动画表现"
                  "（状态照常推进，冷却照常生效）。宿主 %s",
                  *Who);
  }

  bIsRolling = true;
  LastLandRollTime = Now;
  ++LandRollCount;

  WFLOG_INFO("[翻滚] 开始：第 %d 次翻滚（冷却 %.2fs，段名=%s）。%s",
             LandRollCount, LandRollCooldown, *LandRollSectionName.ToString(),
             *GetLandRollDebugString());

  // 可选：把速度清零。只有翻滚靠自己位移（蓝图 Launch / 位移曲线）时才需要，
  // 用 RootMotion 的话速度本来就由动画接管，清零反而会让动作发飘。
  if (bStopMovementOnStart) {
    if (UCharacterMovementComponent *Movement =
            GetOwner()
                ? GetOwner()
                      ->FindComponentByClass<UCharacterMovementComponent>()
                : nullptr) {
      const FVector OldVelocity = Movement->Velocity;
      Movement->StopMovementImmediately();
      WFLOG_INFO(
          "[翻滚] bStopMovementOnStart=true，已清速度 %.1f,%.1f,%.1f。宿主 %s",
          OldVelocity.X, OldVelocity.Y, OldVelocity.Z, *Who);
    }
  }

  // Multicast 会在服务器本地与所有客户端各执行一次（服务器那次是本地执行，
  // 不额外发 RPC），所以这里**不需要**再单独调一次播放——
  // 那会让同一段蒙太奇在服务器上被 Montage_Play 两次。
  //
  // `OnLandRollStarted` 也**不在这里广播**：它由 Multicast 分支在「表现真的播起来之后」
  // 广播，让每个端恰好广播一次，并且避免「广播了 Started 却没有任何表现会结束它」
  // 造成的落锁泄漏（见 Multicast_PlayLandRollMontage_Implementation）。
  Multicast_PlayLandRollMontage(LandRollSectionName);

  if (ActiveLandRollMontage == nullptr) {
    // 没播起来（没配蒙太奇 / 没有 AnimInstance / Montage_Play 被拒）。
    // Multicast 分支已经在权威端就地收尾了：复位 bIsRolling、不落锁、不广播任何事件，
    // 所以这里返回 false（本次翻滚没有真正开始），并把原因说清楚——
    // 「按了翻滚没反应、但冷却被吃掉」是最难查的一类问题。
    WFLOG_WARNING(
        "[翻滚] 蒙太奇没播起来（在播对象为空）：本次翻滚已就地收尾，"
        "既不落移动锁也没有动画（检查 LandRollMontage 与动画蓝图）。宿主 %s",
        *Who);
    return false;
  }

  // 播出去之后才挂兜底定时器（按蒙太奇时长估算）
  OpenRollTimeout();

  return true;
}

bool ULandRollComponent::PlayLandRollMontageInternal(FName InSectionName) {
  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  if (OwnerChar == nullptr) {
    WFLOG_WARNING("[翻滚] 播放失败：宿主 %s 不是 ACharacter。",
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return false;
  }

  // 1) 蒙太奇没配：这是**明确的配置缺失**（不是「合法的空状态」），直接判失败
  if (LandRollMontage == nullptr) {
    WFLOG_WARNING("[翻滚] 播放失败：LandRollMontage 未配置（宿主 %s）。",
                  *OwnerChar->GetName());
    ActiveLandRollMontage = nullptr;
    return false;
  }

  USkeletalMeshComponent *Mesh = OwnerChar->GetMesh();
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    WFLOG_WARNING("[翻滚] 播放失败：宿主 %s 没有 AnimInstance（Mesh=%s）。",
                  *OwnerChar->GetName(),
                  Mesh ? *Mesh->GetName() : TEXT("None"));
    ActiveLandRollMontage = nullptr;
    return false;
  }

  // 2) 回调订阅：BeginPlay 时 AnimInstance 可能还没创建，这里补订。
  //    先 Remove 再 Add，保证不会因为反复翻滚而重复绑定。
  if (!bMontageCallbacksBound) {
    AnimInst->OnMontageEnded.RemoveDynamic(
        this, &ULandRollComponent::OnLandRollMontageEnded);
    AnimInst->OnMontageEnded.AddDynamic(
        this, &ULandRollComponent::OnLandRollMontageEnded);
    AnimInst->OnMontageBlendingOut.RemoveDynamic(
        this, &ULandRollComponent::OnLandRollMontageBlendingOut);
    AnimInst->OnMontageBlendingOut.AddDynamic(
        this, &ULandRollComponent::OnLandRollMontageBlendingOut);
    bMontageCallbacksBound = true;
    WFLOG_INFO("[翻滚] 补订动画回调成功（宿主 %s，AnimInstance=%s）。",
               *OwnerChar->GetName(), *AnimInst->GetClass()->GetName());
  }

  // 3) 真正播放。用返回值判成败：<= 0 表示引擎拒播
  //    （该蒙太奇不在当前状态机里、被 Montage_Stop 之类拒掉等）。
  const float Rate = FMath::Max(MontagePlayRate, KINDA_SMALL_NUMBER);
  const float PlayLength = AnimInst->Montage_Play(LandRollMontage, Rate);
  if (PlayLength <= 0.0f) {
    WFLOG_WARNING(
        "[翻滚] %s 播放失败（Montage_Play 返回 %.2f，播速 %.2f，宿主 %s）。",
        *LandRollMontage->GetName(), PlayLength, Rate, *OwnerChar->GetName());
    ActiveLandRollMontage = nullptr;
    return false;
  }

  // 4) 跳到指定 Section（NAME_None = 留在第 0 段）
  if (!InSectionName.IsNone()) {
    AnimInst->Montage_JumpToSection(InSectionName, LandRollMontage);
    WFLOG_INFO("[翻滚] 已跳到 Section %s。", *InSectionName.ToString());
  }

  // 5) 记录「本段实际在播哪个蒙太奇」：结束回调只认它，
  //    不要用配置项去比对（配置可能被热改，而且动画蓝图可能换了实例）。
  ActiveLandRollMontage = LandRollMontage;

  WFLOG_INFO(
      "[翻滚] 已播放 %s（时长 %.2fs，播速 %.2f，折算后 %.2fs，Section=%s，"
      "宿主 %s）。",
      *LandRollMontage->GetName(), LandRollMontage->GetPlayLength(), Rate,
      GetMontagePlayLength(),
      InSectionName.IsNone() ? TEXT("None") : *InSectionName.ToString(),
      *OwnerChar->GetName());
  return true;
}

void ULandRollComponent::OnLandRollMontageEnded(UAnimMontage *Montage,
                                                bool bInterrupted) {
  WFLOG_INFO("[翻滚] OnMontageEnded：%s（被打断=%d）| 本组件在播=%s | "
             "bIsRolling=%d | 刚从 BlendOut 收尾=%d",
             Montage ? *Montage->GetName() : TEXT("None"), bInterrupted ? 1 : 0,
             ActiveLandRollMontage ? *ActiveLandRollMontage->GetName()
                                   : TEXT("None"),
             bIsRolling ? 1 : 0, bFinishingFromBlendOut ? 1 : 0);

  // 关键判定：**只认本组件正在播的那一个蒙太奇**。
  // 引擎的 OnMontageEnded 会为「任何」蒙太奇触发（受击 / 死亡 / 攀爬…），
  // 不过滤的话会被无关动画把翻滚状态清掉，冷却也就形同虚设了。
  if (Montage == nullptr || Montage != ActiveLandRollMontage) {
    WFLOG_INFO("[翻滚] 该 OnMontageEnded 不是本组件的翻滚蒙太奇，忽略。");
    return;
  }

  if (bFinishingFromBlendOut) {
    // BlendOut 已经收过尾了，这里只清引用（状态不重复复位、事件不重复广播）
    WFLOG_INFO("[翻滚] BlendOut 已收尾，忽略随后到来的 OnMontageEnded。");
    bFinishingFromBlendOut = false;
    ActiveLandRollMontage = nullptr;
    return;
  }

  const TCHAR *Reason =
      bInterrupted ? TEXT("蒙太奇被打断") : TEXT("蒙太奇正常播完");
  FinishLandRoll(Reason, /*bStopMontage=*/false);
}

void ULandRollComponent::OnLandRollMontageBlendingOut(UAnimMontage *Montage,
                                                      bool bInterrupted) {
  if (!bFinishOnBlendOut) {
    return;
  }
  if (Montage == nullptr || Montage != ActiveLandRollMontage) {
    // 不是我们的蒙太奇，静默跳过（这个回调对每个蒙太奇都会触发，打日志会刷屏）
    return;
  }
  if (!bRollPresentationActive) {
    // 已经被超时 / 强制结束收过尾了。
    // ⚠️ 判据必须是本端表现标志，不能是复制的 bIsRolling：它是 COND_OwnerOnly，
    // 模拟代理（其他玩家）永远读不到 true，用它判断会让那些端的翻滚永远收不了尾。
    return;
  }

  WFLOG_INFO("[翻滚] 进入 BlendOut（被打断=%d），bFinishOnBlendOut=true → "
             "提前解锁，不等蒙太奇完全播完。%s",
             bInterrupted ? 1 : 0, *GetLandRollDebugString());

  bFinishingFromBlendOut = true;
  // 这里**不**主动停蒙太奇：让它自然淡出，否则起身动作会被硬切掉。
  FinishLandRoll(TEXT("BlendOut 提前解锁"), /*bStopMontage=*/false);
}

void ULandRollComponent::FinishLandRoll(const TCHAR *Reason,
                                        bool bStopMontage) {
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(RollTimeoutHandle);
  }

  if (bStopMontage && ActiveLandRollMontage != nullptr) {
    if (ACharacter *OwnerChar = Cast<ACharacter>(GetOwner())) {
      if (USkeletalMeshComponent *Mesh = OwnerChar->GetMesh()) {
        if (UAnimInstance *AnimInst = Mesh->GetAnimInstance()) {
          AnimInst->Montage_Stop(/*InBlendOutTime=*/0.1f,
                                 ActiveLandRollMontage);
          WFLOG_INFO("[翻滚] 已主动停掉蒙太奇 %s（0.1s 淡出）。",
                     *ActiveLandRollMontage->GetName());
        }
      }
    }
  }

  // 收尾要对两类状态分开处理：
  //   * 表现标志 bRollPresentationActive：**每个端**都要写，它决定要不要广播 Finished；
  //   * 玩法状态 bIsRolling：**只有权威端**能写（客户端不写玩法状态：
  //     拥有者客户端的值由服务器复制下来，模拟代理则根本收不到这个属性）。
  const bool bAuthority = IsAuthoritativeForActorComponent(this);
  const bool bWasPresenting = bRollPresentationActive;
  const bool bWasRolling = bIsRolling;

  bRollPresentationActive = false;
  ActiveLandRollMontage = nullptr;
  if (bAuthority) {
    bIsRolling = false;
  }

  WFLOG_INFO("[翻滚] 结束：%s（原表现中=%d，原 bIsRolling=%d，累计 %d 次，本端权威=%d）。%s",
             Reason, bWasPresenting ? 1 : 0, bWasRolling ? 1 : 0, LandRollCount,
             bAuthority ? 1 : 0, *GetLandRollDebugString());

  // Started / Finished 严格配对：只有本端真的广播过 Started（= bWasPresenting）
  // 才广播 Finished。不配对时宁可少广播一次，也不要让订阅者收到「无配对的解锁请求」——
  // 那会让「锁的持有者集合」与实际状态不一致，排查时反而更乱。
  if (bWasPresenting) {
    // 通知订阅者（释放移动锁、回到待机等）。服务器与各客户端都会各自广播一次。
    OnLandRollFinished.Broadcast();
    WFLOG_INFO("[翻滚] 已广播 OnLandRollFinished（订阅者会释放移动锁）。");
  }
}

void ULandRollComponent::ResetLandRollCooldown() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  LastLandRollTime = -1000.0f;
  WFLOG_INFO("[翻滚] 冷却已清除（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
}

void ULandRollComponent::ForceFinishLandRoll_Implementation() {
  WF_COMPONENT_AUTHORITY_GUARD(void());

  // 判据同时看玩法状态与本端表现：正常情况下两者同进同出，
  // 但「表现未能开始」那条路径会先复位 bIsRolling，这里也要能正确判空操作。
  if (!bIsRolling && !bRollPresentationActive) {
    WFLOG_INFO("[翻滚] ForceFinishLandRoll：当前并不在翻滚中（bIsRolling=0，表现中=0），"
               "空操作。宿主 %s",
               GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return;
  }

  WFLOG_INFO(
      "[翻滚] ForceFinishLandRoll：强制收尾（受击 / 眩晕等打断）。宿主 %s",
      GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  FinishLandRoll(TEXT("外部强制结束"), /*bStopMontage=*/true);
}

// ===== 表现同步 =====

void ULandRollComponent::Multicast_PlayLandRollMontage_Implementation(
    FName InSectionName) {
  const bool bAuthority = IsAuthoritativeForActorComponent(this);

  // ⚠️ 本函数在**每个端**都会执行一次，而且「客户端自己调用」时也只会在本端执行
  // （引擎行为：Multicast 在客户端的 callspace 是 Local，既不发服务器也不广播给别人，
  // 见头文件里对 `AActor::GetFunctionCallspace` 的引用）。
  // 所以它必须对「任何端、任何调用来源」都安全：**只播动画 + 维护本端表现标志，
  // 不写任何玩法状态**（bIsRolling / LastLandRollTime 只由权威端写）。
  WFLOG_INFO("[翻滚] Multicast_PlayLandRollMontage 到达（%s）：Section=%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             InSectionName.IsNone() ? TEXT("None") : *InSectionName.ToString());

  // 无门禁内核：客户端分支只播动画；段名由服务器随 RPC 带来，
  // 所以客户端与服务器播的一定是同一段。
  const bool bPlayed = PlayLandRollMontageInternal(InSectionName);

  if (!bPlayed) {
    // 表现没起来：**一个事件都不广播**。
    // Started / Finished 必须严格配对——广播了 Started 却没有蒙太奇来触发结束，
    // 订阅者（移动门控）的锁就永远挂在那里，角色再也动不了。
    WFLOG_WARNING(
        "[翻滚] Multicast 之后没有在播的蒙太奇（Section=%s）：本次翻滚没有动画表现，"
        "也不会广播 OnLandRollStarted。%s",
        InSectionName.IsNone() ? TEXT("None") : *InSectionName.ToString(),
        *GetLandRollDebugString());

    if (bAuthority) {
      // 权威端此时 bIsRolling 已经置位，而「没有任何东西会来结束它」——
      // 必须就地收尾，否则重入判定会一直拒绝新的翻滚、冷却也永远在走。
      // FinishLandRoll 不会广播（bRollPresentationActive 还是 false），
      // 所以不会产生无配对的 Finished。
      WFLOG_WARNING("[翻滚] 权威端就地收尾：本次翻滚没有任何东西会来结束它，"
                    "现在复位玩法状态以免卡住重入与冷却。宿主 %s",
                    GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
      FinishLandRoll(TEXT("翻滚表现未能开始"), /*bStopMontage=*/false);
    }
    return;
  }

  // 表现真的播起来了：置本端标志 + 广播 Started。
  // 顺序是有意的：**先置标志再广播**，订阅者收到 Started 后若立刻回头查询
  // `IsRollPresentationActive()`，读到的必须已经是 true。
  bRollPresentationActive = true;
  OnLandRollStarted.Broadcast();
  WFLOG_INFO("[翻滚] %s 已广播 OnLandRollStarted（订阅者会落移动锁）。%s",
             bAuthority ? TEXT("服务器") : TEXT("客户端"),
             *GetLandRollDebugString());
}

void ULandRollComponent::Multicast_PlayLandRollEffects_Implementation() {
  // ⚠️ 这里**不能**加「非权威端就忽略」的门禁：Multicast 在每个端都会执行，
  // 客户端收到的那一次本来就不是权威端——加了门禁等于客户端永远看不到特效。
  // 表现同步要的就是「每个端各自本地播一遍」。
  WFLOG_INFO("[翻滚] 广播翻滚表现（PlayLandRollEffects 蓝图事件，本端权威=%d）。宿主 %s",
             IsAuthoritativeForActorComponent(this) ? 1 : 0,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  PlayLandRollEffects();
}

void ULandRollComponent::PlayLandRollEffects_Implementation() {
  // 本地表现入口：蓝图覆写本事件即可（尘土 / 音效 / 镜头震动）。C++
  // 默认什么都不做。
}

// ===== 客户端 -> 服务器 =====

bool ULandRollComponent::Server_LandRoll_Validate() {
  // 无参数，没有可校验的输入；真正的重入 / 冷却 / 总开关判定在 LandRoll() 内部
  return true;
}

void ULandRollComponent::Server_LandRoll_Implementation() {
  // 服务器（含单机 / 监听服务器）上 Server RPC 就地同步执行，
  // 所以单机与联机走的是同一条路径，不需要写 HasAuthority() 分支。
  WFLOG_INFO("[翻滚] RPC Server_LandRoll 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  LandRoll();
}
