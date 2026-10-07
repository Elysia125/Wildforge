// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/AttackComponent.h"

#include "Animation/AnimInstance.h"
#include "CollisionQueryParams.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"

// 开发期门禁：本类的权威函数都是
// BlueprintAuthorityOnly，客户端调用会被引擎静默丢弃 （callspace =
// Absorbed）——单机看不出来、联机才失效，这类 bug 极难查。
// 统一用本宏在非权威端立刻报错并安全返回（Shipping 下被裁掉，只剩日志）。
// 客户端请改调对应的 Server_* RPC。
// 注意：HasAuthority() 是 AActor 的方法，组件上不存在（会报 C3861），必须经
// GetOwner() 转发——这里用 Utils/WildforgeAuthority.h 的辅助函数。
// 与 UItemContainer.cpp 顶部同款写法，保持全项目一致。
#define WF_ATTACK_AUTHORITY_GUARD(RetVal)                                      \
  do {                                                                         \
    if (!IsAuthoritativeForActorComponent(this)) {                             \
      WFLOG_ERROR(                                                             \
          "%s 在非权威端被调用，已忽略；客户端请改用对应的 Server_* RPC。",    \
          *FString(__FUNCTION__));                                             \
      return RetVal;                                                           \
    }                                                                          \
  } while (false)

// Sets default values for this component's properties
UAttackComponent::UAttackComponent() {
  // 攻击流程是事件驱动（蒙太奇 / 动画通知 / RPC），不需要每帧 Tick
  PrimaryComponentTick.bCanEverTick = false;
}

void UAttackComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 这几个都是「服务器写、客户端只读」的状态。
  // COND_SimulatedOnly：只发给模拟端 ——
  // 服务器自己不需要收，自主代理（本地玩家）
  // 也不需要（它的动作本来就是本地输入驱动的），这样能省掉一份无用带宽。
  DOREPLIFETIME_CONDITION(UAttackComponent, bIsAttacking, COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(UAttackComponent, bCanCombo, COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(UAttackComponent, MontageSectionIndex,
                          COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(UAttackComponent, AttackMontageIndex,
                          COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(UAttackComponent, LastAttackTime, COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(UAttackComponent, ActiveAttackMontage,
                          COND_SimulatedOnly);
}

// Called when the game starts
void UAttackComponent::BeginPlay() {
  Super::BeginPlay();

  // ...
}

void UAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // 组件销毁 / 角色销毁时清掉兜底定时器，避免回调打到半销毁的对象上
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackTimeoutHandle);
  }
  Super::EndPlay(EndPlayReason);
}

// Called every frame
void UAttackComponent::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // ...
}

// ===== 服务器权威：攻击流程 =====
// 设计要点：
//   1. 「打哪一段」由服务器决定（MontageSectionIndex / AttackMontageIndex），
//      客户端只能请求、不能指定，否则等于可以自选伤害段。
//   2. 蒙太奇播放必须 Multicast：引擎默认不复制蒙太奇，只在服务器 Montage_Play
//      客户端是看不到动画的（见头文件里的说明）。
//   3. 连击通知在蒙太奇**播放中途**发出：窗口一开，玩家再点就**立刻切播下一段**
//      （不等当前动画播完）。「本段正在播」不等于「不接受输入」——曾经用一个
//      `bAttackMontageInProgress` 在最前面直接 return，把连击全挡掉了。

// 攻击状态的一行快照：连击接不上时能直接从日志看出卡在哪一步
// （RPC 有没有到服务器 → 连击窗口有没有开 → 输入有没有被拒 → 有没有下一段可播）。
FString UAttackComponent::GetAttackStateDebugString() const {
  return FString::Printf(
      TEXT("bIsAttacking=%d bCanCombo=%d MontageIndex=%d SectionIndex=%d "
           "ActiveMontage=%s 距本段开始=%.3fs"),
      bIsAttacking ? 1 : 0, bCanCombo ? 1 : 0, AttackMontageIndex,
      MontageSectionIndex,
      ActiveAttackMontage ? *ActiveAttackMontage->GetName() : TEXT("None"),
      GetWorld() ? GetWorld()->GetTimeSeconds() - LastAttackTime : -1.0f);
}

bool UAttackComponent::Attack_Implementation(bool bIsInputDriven) {
  WF_ATTACK_AUTHORITY_GUARD(false);

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[攻击] Attack 进入（输入驱动=%d）：%s", bIsInputDriven ? 1 : 0,
             *GetAttackStateDebugString());

  // 总开关：眩晕 / 缴械 / 死亡等状态把它关掉后，起手与连击一律不生效
  if (!bCanAttack) {
    WFLOG_WARNING("[攻击] Attack 被拒：bCanAttack=false（总开关关着）。宿主 %s",
                  *Who);
    return false;
  }

  UWorld *World = GetWorld();
  if (World == nullptr) {
    WFLOG_WARNING("[攻击] Attack 被拒：World 为空。宿主 %s", *Who);
    return false;
  }

  const float Now = World->GetTimeSeconds();

  // ---- 情况 A：攻击链进行中 → 连击接招（立刻播下一段）----
  if (bIsAttacking) {
    // 连击窗口由蒙太奇中途的动画通知打开；窗口没开 = 这一段不给接
    if (!bCanCombo) {
      WFLOG_INFO("[攻击] 连击被拒：连击窗口没开（bCanCombo=false）。说明动画里的 "
                 "UAnimNotify_Combo / Server_NotifyComboWindow 没生效，或已经"
                 "被这一次输入用掉了。宿主 %s", *Who);
      return false;
    }
    // 连击的节流单位是「窗口」，不是「蒙太奇段」。
    // ⚠️ 千万不要写成「一段蒙太奇只收一次输入」来防连点：起手那一次输入之后
    // 整个蒙太奇期间都不会有「新的按下」来复位标志，于是窗口一开所有点击全被拒，
    // 连击永远接不上（本项目实测踩过：见 bug-022）。
    // 窗口本身每段都会被 UAnimNotify_Combo 重开，用它当节流单位天然正确。
    // 这里只剩一道时间下限：防止被篡改的客户端在同一帧刷出几十个请求、
    // 一口气把整条连招打完。默认值远小于窗口之间的间隔，不影响手感。
    if (Now - LastAttackTime < ComboMinInterval) {
      WFLOG_INFO("[攻击] 连击被拒：距上一段开始只过了 %.3fs（ComboMinInterval="
                 "%.3f）。宿主 %s", Now - LastAttackTime, ComboMinInterval,
                 *Who);
      return false;
    }

    // 当前段的起始时间（接招时用来算交叉淡入时长）
    const float PrevSegmentStart = LastAttackTime;

    // 窗口是「一次机会」：接招的同时就关掉，等下一段的通知重新开
    bCanCombo = false;

    WFLOG_INFO("[攻击] 连击窗口已开 → 立刻切下一段（下一段=M%d/S%d）。宿主 %s",
               AttackMontageIndex, MontageSectionIndex, *Who);

    // 本次蒙太奇结束回调是我们自己切段引发的，标记一下别当成「链结束」
    bAdvancingCombo = true;
    const bool bAdvanced = AdvanceAttackChain(PrevSegmentStart);
    bAdvancingCombo = false;

    if (!bAdvanced) {
      // 没有下一段可用（连招打完了）：结束整条链，恢复移动
      WFLOG_WARNING("[攻击] 连击切段失败：没有下一段可播（M%d/S%d 越界或没配"
                    "蒙太奇），攻击链结束。宿主 %s",
                    AttackMontageIndex, MontageSectionIndex, *Who);
      FinishAttackChain(TEXT("连招打完，没有下一段"));
    } else {
      WFLOG_INFO("[攻击] 连击切段成功：%s", *GetAttackStateDebugString());
    }
    return bAdvanced;
  }

  // ---- 情况 B：空闲 → 起手 ----
  if (Now - LastAttackTime < AttackCooldown) {
    WFLOG_INFO("[攻击] 起手被拒：距上次起手 %.3fs < AttackCooldown %.3fs。宿主 %s",
               Now - LastAttackTime, AttackCooldown, *Who);
    return false;
  }

  const int32 StartMontage = AttackMontageList.IsValidIndex(AttackMontageIndex)
                                 ? AttackMontageIndex
                                 : 0;
  const int32 StartSection = MontageSectionIndex;

  bIsAttacking = true;
  LastAttackTime = Now;
  bCanCombo = false;

  WFLOG_INFO("[攻击] 起手：播 M%d/S%d（列表 %d 个蒙太奇条目）。宿主 %s",
             StartMontage, StartSection, AttackMontageList.Num(), *Who);

  // Multicast 会在服务器本地与所有客户端各执行一次（服务器那次是本地执行，
  // 不额外发 RPC），所以这里**不需要**再单独调一次 PlayAttackMontage——
  // 那会让同一段蒙太奇在服务器上被 Montage_Play 两次。
  // 起手用硬切（淡入 0）：从待机动作切进攻击要干脆，交叉淡入反而糊。
  Multicast_PlayAttackMontage(StartMontage, StartSection,
                              /*InFadeInTime=*/0.0f);

  if (ActiveAttackMontage == nullptr) {
    // 没播出去（蒙太奇列表为空 / 没有 AnimInstance / Montage_Play 被拒）：
    // 回滚状态，避免角色卡在「攻击中」不能移动。
    FinishAttackChain(TEXT("起手播放失败"));
    return false;
  }

  // 播出去之后才推进下标：下标现在指向「下一次要播的那一段」
  AdvanceAttackSection();

  // 表现层的两种挂法（二选一，别同时用，否则效果会放两遍）：
  //   1. 挥砍/起手类表现：在这里调
  //   Multicast_PlayAttackEffects()，随攻击一起同步；
  //   2. 命中类表现：放在动画通知 UAnimNotify_AttackHit 里调
  //   PlayAttackEffects()，
  //      蒙太奇已经同步到所有端，每端命中那一帧各自放一次即可。
  return true;
}

void UAttackComponent::AdvanceAttackSection() {
  MontageSectionIndex++;
  if (AttackMontageList.IsValidIndex(AttackMontageIndex) &&
      MontageSectionIndex >=
          AttackMontageList[AttackMontageIndex].SectionNames.Num()) {
    MontageSectionIndex = 0;
    AttackMontageIndex++;
    if (AttackMontageIndex >= AttackMontageList.Num()) {
      AttackMontageIndex = 0;
    }
  }
}

bool UAttackComponent::AdvanceAttackChain(float PrevSegmentStartTime) {
  // 当前下标已经指向「下一段」（起手 / 上一次接招之后立刻推进过）
  const int32 NextMontage = AttackMontageList.IsValidIndex(AttackMontageIndex)
                                ? AttackMontageIndex
                                : 0;
  const int32 NextSection = MontageSectionIndex;

  const FAttackMontageData *Candidate =
      AttackMontageList.IsValidIndex(NextMontage)
          ? &AttackMontageList[NextMontage]
          : nullptr;
  if (Candidate == nullptr || Candidate->Montage == nullptr ||
      !Candidate->SectionNames.IsValidIndex(NextSection)) {
    WFLOG_WARNING("[攻击] 连击找不到下一段：M%d 越界=%d，蒙太奇为空=%d，"
                  "SectionNames 数=%d（要 SectionIndex=%d）。宿主 %s",
                  NextMontage,
                  AttackMontageList.IsValidIndex(NextMontage) ? 0 : 1,
                  (Candidate && Candidate->Montage) ? 0 : 1,
                  Candidate ? Candidate->SectionNames.Num() : -1, NextSection,
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return false; // 没有下一段可播 = 连招打完
  }

  // 立刻切播下一段：Montage_Play 会停掉当前蒙太奇（引擎内部触发一次
  // OnMontageEnded(bInterrupted=true)，被上面的 bAdvancingCombo 标记挡掉，
  // 不会被误判成「攻击链结束」）。
  //
  // 交叉淡入时长：上一位作者通常把连击窗口摆在挥砍动作「已经打出去」的位置，
  // 所以这一段已经播了多久 = 可以用来做衔接的淡入时长。硬切会看到抽帧。
  const float FadeInTime = FMath::Clamp(
      GetWorld()->GetTimeSeconds() - PrevSegmentStartTime, 0.05f, 0.2f);

  WFLOG_INFO("[攻击] 连击切段：M%d/S%d（%s），交叉淡入 %.3fs。宿主 %s",
             NextMontage, NextSection, *Candidate->Montage->GetName(), FadeInTime,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));

  Multicast_PlayAttackMontage(NextMontage, NextSection, FadeInTime);
  if (ActiveAttackMontage == nullptr) {
    WFLOG_WARNING("[攻击] 连击切段失败：Multicast 之后 ActiveAttackMontage 仍为"
                  "空（M%d/S%d）。宿主 %s",
                  NextMontage, NextSection,
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return false;
  }

  // 接招成功：这一段从通知重开窗口时再接受下一次输入
  LastAttackTime = GetWorld()->GetTimeSeconds();
  AdvanceAttackSection();
  return true;
}

bool UAttackComponent::PlayAttackMontage_Implementation(int32 InMontageIndex,
                                                        int32 InSectionIndex,
                                                        float InFadeInTime) {
  WF_ATTACK_AUTHORITY_GUARD(false);
  return PlayAttackMontageInternal(InMontageIndex, InSectionIndex,
                                   InFadeInTime);
}

bool UAttackComponent::PlayAttackMontageInternal(int32 InMontageIndex,
                                                 int32 InSectionIndex,
                                                 float InFadeInTime) {
  if (!AttackMontageList.IsValidIndex(InMontageIndex)) {
    WFLOG_WARNING("PlayAttackMontage: MontageIndex %d 越界（列表 %d 项），"
                  "攻击未播放。",
                  InMontageIndex, AttackMontageList.Num());
    return false;
  }

  const FAttackMontageData &Data = AttackMontageList[InMontageIndex];
  if (Data.Montage == nullptr) {
    WFLOG_WARNING("PlayAttackMontage: AttackMontageList[%d] 没有配蒙太奇，"
                  "攻击未播放。",
                  InMontageIndex);
    return false;
  }
  if (!Data.SectionNames.IsValidIndex(InSectionIndex)) {
    WFLOG_WARNING("PlayAttackMontage: %s 没有第 %d 个 Section（共 %d 个），"
                  "攻击未播放。",
                  *Data.Montage->GetName(), InSectionIndex,
                  Data.SectionNames.Num());
    return false;
  }

  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  if (OwnerChar == nullptr) {
    WFLOG_WARNING("PlayAttackMontage: 宿主 %s 不是 ACharacter，攻击未播放。",
                  GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
    return false;
  }

  USkeletalMeshComponent *Mesh = OwnerChar->GetMesh();
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    WFLOG_WARNING("PlayAttackMontage: 宿主 %s 没有 AnimInstance，攻击未播放。",
                  *OwnerChar->GetName());
    return false;
  }

  // 先解绑再绑定，避免同一回调被重复注册（每次攻击都会走这里）
  AnimInst->OnMontageEnded.RemoveDynamic(
      this, &UAttackComponent::OnAttackMontageEnded);
  AnimInst->OnMontageEnded.AddDynamic(this,
                                      &UAttackComponent::OnAttackMontageEnded);

  // 交叉淡入：连击立刻切段时用它做衔接（0 = 硬切，起手时就是这样）
  const float PlayLength =
      InFadeInTime > 0.0f ? AnimInst->Montage_PlayWithBlendIn(
                                Data.Montage, FAlphaBlendArgs(InFadeInTime))
                          : AnimInst->Montage_Play(Data.Montage);
  if (PlayLength <= 0.0f) {
    // 播放失败（该蒙太奇没在动的状态被拒等）：不置位，让调用方回滚
    WFLOG_WARNING("PlayAttackMontage: %s 播放失败（返回 %.2f）。",
                  *Data.Montage->GetName(), PlayLength);
    ActiveAttackMontage = nullptr;
    return false;
  }
  if (!Data.SectionNames[InSectionIndex].IsNone()) {
    AnimInst->Montage_JumpToSection(Data.SectionNames[InSectionIndex],
                                    Data.Montage);
  }

  // 记录「本段在播哪个蒙太奇」：结束回调只认这个，不要用会提前推进的下标去查表
  ActiveAttackMontage = Data.Montage;
  WFLOG_INFO("%s 播放攻击蒙太奇 %s（MontageIndex=%d, SectionIndex=%d, "
             "时长 %.2fs）。",
             *OwnerChar->GetName(), *Data.Montage->GetName(), InMontageIndex,
             InSectionIndex, PlayLength);
  return true;
}

void UAttackComponent::Multicast_PlayAttackMontage_Implementation(
    int32 InMontageIndex, int32 InSectionIndex, float InFadeInTime) {
  const bool bAuthority = IsAuthoritativeForActorComponent(this);
  WFLOG_INFO("[攻击] Multicast_PlayAttackMontage 到达（%s）：M%d/S%d，淡入 %.3fs",
             bAuthority ? TEXT("服务器") : TEXT("客户端"), InMontageIndex,
             InSectionIndex, InFadeInTime);

  if (bAuthority) {
    // 服务器本地那一次：走权威入口（带门禁）。功能上等价于直接调内核
    // （门禁在权威端必然通过），差别只是多一次检查与一条日志。
    PlayAttackMontage(InMontageIndex, InSectionIndex, InFadeInTime);
  } else {
    // 客户端分支：**只写「本端在播哪个蒙太奇」这个表现层标记**
    // （PlayAttackMontageInternal 会写 ActiveAttackMontage，供 OnAttackMontageEnded
    // 做归属判定），**绝不碰** bIsAttacking / 连击窗口 / 段位下标这些玩法状态
    // ——它们仍以服务器复制为准。
    //
    // 为什么这里不调带门禁的权威入口：Multicast 在客户端本地被调用时（引擎允许），
    // 门禁会记一条 ERROR 并**拒绝播放**；而这里的语义应该是「退化成纯本地表现」。
    // 参数由服务器随 RPC 带来，所以客户端与服务器播的一定是同一段。
    PlayAttackMontageInternal(InMontageIndex, InSectionIndex, InFadeInTime);
  }

  if (ActiveAttackMontage != nullptr) {
    // 广播给订阅者（禁止移动等）。服务器与各客户端各自在自己的机器上收到一次。
    // 连击切段时也会再广播一次——订阅者用「已经在禁止移动」判断做了幂等，
    // 所以不会重复操作（见 APlayerCharacter::HandleAttackStarted）。
    OnAttackStarted.Broadcast();
    WFLOG_INFO("[攻击] 已广播 OnAttackStarted：%s",
               *GetAttackStateDebugString());
  } else {
    WFLOG_WARNING("[攻击] Multicast_PlayAttackMontage：蒙太奇没播起来，"
                  "不广播 OnAttackStarted（M%d/S%d）",
                  InMontageIndex, InSectionIndex);
  }
}

void UAttackComponent::Multicast_PlayAttackEffects_Implementation() {
  // ⚠️ 这里**刻意不加权威门禁**（曾经是「非权威端就记 ERROR 并 return」）：
  //    Multicast 在每个端都会执行，客户端那一次是**正常接收**，门禁会让攻击特效
  //    （打击感 / 音效 / 粒子）只在服务器上播。
  //    引擎规则（Actor.cpp:5500-5519）：Multicast 在客户端只返回 Local，不会再转发，
  //    所以「客户端调用刷屏」不成立。与翻滚组件 `Multicast_PlayLandRollEffects` 一致。
  WFLOG_INFO("[攻击] 广播攻击表现（PlayAttackEffects 蓝图事件，本端权威=%d）。宿主 %s",
             IsAuthoritativeForActorComponent(this) ? 1 : 0,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  PlayAttackEffects();
}

void UAttackComponent::OnAttackMontageEnded(UAnimMontage *Montage,
                                            bool bInterrupted) {
  WFLOG_INFO("[攻击] OnMontageEnded：%s（被打断=%d）| 当前记录的在播蒙太奇=%s | "
             "bAdvancingCombo=%d",
             Montage ? *Montage->GetName() : TEXT("None"), bInterrupted ? 1 : 0,
             ActiveAttackMontage ? *ActiveAttackMontage->GetName()
                                 : TEXT("None"),
             bAdvancingCombo ? 1 : 0);

  // 只认「本段实际在播的那个蒙太奇」。这里刻意不用 AttackMontageIndex 去查表：
  // 服务器推进下标与客户端收到复制的顺序不保证，查表会错配（见头文件说明）。
  if (Montage == nullptr || Montage != ActiveAttackMontage) {
    // 不是我们正在播的攻击蒙太奇（其他系统的蒙太奇，或上一段还在混合输出），不理
    WFLOG_INFO("[攻击] 该 OnMontageEnded 不属于当前攻击段，忽略。");
    return;
  }

  ActiveAttackMontage = nullptr;

  // 落在这里有两种原因：
  //   1. 我们自己为了连击切段而停掉了上一段（bAdvancingCombo == true）
  //      —— 不是「链结束」，直接返回，链条由 AdvanceAttackChain 接着走；
  //   2. 当前段真的播完了 / 被其他系统打断。
  if (bAdvancingCombo) {
    return;
  }

  // 本段播完（或被中途打断）了，清掉兜底定时器
  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackTimeoutHandle);
  }

  // 被打断（死亡 / 受击硬直 / 其他系统 StopMontage）就收尾
  const TCHAR *Reason =
      bInterrupted ? TEXT("蒙太奇被打断") : TEXT("攻击链正常结束");
  // 这一段播完了但没有连击接上 → 整条链结束（订阅者据此恢复移动）
  FinishAttackChain(Reason);
}

void UAttackComponent::FinishAttackChain(const TCHAR *Reason) {
  WFLOG_INFO("[攻击] FinishAttackChain：%s。宿主 %s", Reason,
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));

  // 复位状态（含关窗口、清输入标记、清定时器）
  ResetAttackState();

  // 通知订阅者（恢复移动、切回待机等）。服务器与各客户端都会各自收到。
  OnAttackFinished.Broadcast();
  WFLOG_INFO("[攻击] 已广播 OnAttackFinished（订阅者会恢复移动）。");
}

void UAttackComponent::ResetAttackState_Implementation() {
  WF_ATTACK_AUTHORITY_GUARD(void());

  if (UWorld *World = GetWorld()) {
    World->GetTimerManager().ClearTimer(AttackTimeoutHandle);
  }

  bIsAttacking = false;
  // 连击窗口必须跟着清：不然被打断后残留的窗口会让下一次起手直接接招
  bCanCombo = false;
  bAdvancingCombo = false;
  MontageSectionIndex = 0;
  AttackMontageIndex = 0;
  ActiveAttackMontage = nullptr;
  LastAttackTime = 0.0f;
}

float UAttackComponent::GetMontagePlayLength(int32 InMontageIndex) const {
  if (AttackMontageList.IsValidIndex(InMontageIndex)) {
    const UAnimMontage *Montage = AttackMontageList[InMontageIndex].Montage;
    if (Montage != nullptr) {
      // 注意用 GetPlayLength()：UE 5.7 里直接读 SequenceLength 已被弃用，
      // 而且它还是 protected（直接访问编译不过）
      const float Length = Montage->GetPlayLength();
      if (Length > 0.0f) {
        return Length;
      }
    }
  }
  return 1.0f;
}

void UAttackComponent::HandleAttackTimeout() {
  if (!bIsAttacking) {
    return;
  }
  WFLOG_WARNING("攻击蒙太奇超时未结束，强制收尾复位（宿主 %s）。",
                GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  FinishAttackChain(TEXT("蒙太奇超时未结束"));
}

void UAttackComponent::CloseComboWindow() {
  WF_ATTACK_AUTHORITY_GUARD(void());
  WFLOG_INFO("[攻击] CloseComboWindow：关闭连击窗口。宿主 %s",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  bCanCombo = false;
}

void UAttackComponent::OpenComboWindow() {
  WF_ATTACK_AUTHORITY_GUARD(void());

  const FString Who = GetOwner() ? GetOwner()->GetName() : TEXT("None");
  WFLOG_INFO("[攻击] OpenComboWindow 调用：%s", *GetAttackStateDebugString());

  // 只在攻击过程中开窗：防客户端在没出招时凭空打开连击窗口
  if (!bIsAttacking) {
    WFLOG_WARNING("[攻击] 连击窗口没开：当前不在攻击中（bIsAttacking=false，"
                  "动画通知可能在非攻击蒙太奇上触发）。宿主 %s", *Who);
    return;
  }
  if (bCanCombo) {
    WFLOG_INFO("[攻击] 连击窗口已经是开的，忽略重复通知。宿主 %s", *Who);
    return;
  }

  bCanCombo = true;

  WFLOG_INFO("[攻击] 连击窗口已打开（现在点击即可接下一段）。宿主 %s", *Who);

  // 兜底：连击窗口开了之后，如果攻击蒙太奇因为动画异常（比如没触发
  // OnMontageEnded）一直不结束，角色会永久卡在「攻击中不能移动」。
  // 这里按蒙太奇时长挂一个一次性定时器强制收尾；蒙太奇正常结束会清掉它。
  if (UWorld *World = GetWorld()) {
    FTimerDelegate TimeoutDelegate;
    TimeoutDelegate.BindUObject(this, &UAttackComponent::HandleAttackTimeout);
    World->GetTimerManager().SetTimer(
        AttackTimeoutHandle, TimeoutDelegate,
        FMath::Max(1.0f, GetMontagePlayLength(AttackMontageIndex)), false);
  }
}

// ===== 服务器权威：伤害判定 =====
//
// 「让攻击组件无需知道被攻击的是什么」的做法：**不自己扣血，只发伤害**。
// 引擎的伤害管线（UGameplayStatics::ApplyDamage → AActor::TakeDamage → 蓝图
// AnyDamage 事件）就是为此设计的：攻击方只提供一个数值 + 命中信息，
// 受击方自己决定「我是谁、我怎么掉血、我有多少护甲、我是不是无敌」。
//
//   * 组件只依赖 AActor 这一个基类，不认识 Enemy / PlayerCharacter / 场景道具；
//   * 打中不能受伤的东西（`CanBeDamaged() == false`）直接跳过，不需要类型判断；
//   * 想接入新的受击者，写一个 `TakeDamage` 覆写或绑蓝图 `AnyDamage` 就行，
//     攻击组件一行都不用改（这就是依赖倒置：检测器不认识被检测者）；
//   * 需要更复杂的抗性/元素/护甲结算时，把 ApplyDamage 换成
//     UGameplayStatics::ApplyPointDamage，或让受击者自己接管 GameplayEffect，
//     组件侧依然只提供「伤害值 + 命中结构体」。
//
// 另一半解耦靠**扫掠检测**本身：`SweepMultiByChannel` 返回的是
// `TArray<FHitResult>`（纯几何结果），组件根本不参与「什么该被打」的判定——
// 那是碰撞通道 / 物体响应配置的事。
void UAttackComponent::PerformDamageTrace_Implementation() {
  WF_ATTACK_AUTHORITY_GUARD(void());

  UWorld *World = GetWorld();
  AActor *Owner = GetOwner();
  if (World == nullptr || Owner == nullptr) {
    return;
  }

  if (AttackDamage <= 0.0f) {
    return;
  }

  // 起点：角色原点（胶囊体中心）抬高到胸口；方向：角色朝向（含俯仰，可上/下劈砍）
  const FVector TraceStart =
      Owner->GetActorLocation() +
      FVector(0.0f, 0.0f, AttackTraceHeightOffset * Owner->GetActorScale3D().Z);
  const FVector TraceEnd =
      TraceStart + Owner->GetActorForwardVector() * AttackRange;

  // 检测形状：半径 <= 0 时退化成细线段（等价于 LineTraceSingleByChannel）
  const FCollisionShape TraceShape =
      AttackTraceRadius > 0.0f ? FCollisionShape::MakeSphere(AttackTraceRadius)
                               : FCollisionShape::MakeSphere(0.0f);

  FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AttackDamageTrace),
                                    /*bTraceComplex=*/false, Owner);
  // 挂在宿主身上的子 Actor（手里的武器等）不该被自己打中
  QueryParams.AddIgnoredActors(Owner->Children);

  TArray<FHitResult> Hits;
  World->SweepMultiByChannel(Hits, TraceStart, TraceEnd, FQuat::Identity,
                             AttackTraceChannel, TraceShape, QueryParams);

  // 同一次扫掠可能对同一目标产生多个命中点，去重后只结算一次
  TSet<AActor *> ProcessedActors;
  int32 DamagedCount = 0;
  for (const FHitResult &Hit : Hits) {
    AActor *HitActor = Hit.GetActor();
    if (HitActor == nullptr || HitActor == Owner) {
      continue;
    }
    if (ProcessedActors.Contains(HitActor)) {
      continue;
    }
    ProcessedActors.Add(HitActor);

    // 不能受伤的目标（特效体、装饰物等）本来就是合法的命中结果，静默跳过
    if (!HitActor->CanBeDamaged()) {
      continue;
    }

    // 引擎伤害管线：伤害值由攻击组件提供，结算方式由受击方自己决定
    UGameplayStatics::ApplyDamage(HitActor, AttackDamage,
                                  Owner->GetInstigatorController(), Owner,
                                  UDamageType::StaticClass());
    ++DamagedCount;
  }

  WFLOG_INFO(
      "PerformDamageTrace: %s 命中 %d 次，实际结算 %d 个目标（伤害 %.1f，"
      "射程 %.1f）。",
      *Owner->GetName(), Hits.Num(), DamagedCount, AttackDamage, AttackRange);
}

// ===== 表现同步 =====

void UAttackComponent::PlayAttackEffects_Implementation() {
  // 本地表现入口：蓝图覆写本事件即可（生成特效 / 播音效 / 镜头震动）。
  // C++ 默认什么都不做。
}

// ===== 客户端 -> 服务器：攻击请求 =====

bool UAttackComponent::Server_Attack_Validate() {
  // 无参数，没有可校验的输入；真正的冷却 / 连击 / 段位判定在 Attack() 内部
  return true;
}

void UAttackComponent::Server_Attack_Implementation() {
  // 服务器（含单机 / 监听服务器）上 Server RPC 就地同步执行，
  // 所以单机与联机走的是同一条路径，不需要写 HasAuthority() 分支。
  // bIsInputDriven = true：每段只接受一次玩家输入，必须松开再按。
  WFLOG_INFO("[攻击] RPC Server_Attack 到达服务器（宿主 %s）。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  Attack(true);
}

bool UAttackComponent::Server_NotifyComboWindow_Validate() {
  // 无参数；窗口真的要不要开由 OpenComboWindow() 在服务器侧判定
  return true;
}

void UAttackComponent::Server_NotifyComboWindow_Implementation() {
  // 这条日志是判断「动画通知有没有生效」的关键：只有它出现了，
  // 才说明 UAnimNotify_Combo 在服务器上跑到了、并且 RPC 成功到达。
  WFLOG_INFO("[攻击] RPC Server_NotifyComboWindow 到达服务器（宿主 %s）——"
             "动画里的连击通知生效。",
             GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
  OpenComboWindow();
}
