// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Player/PlayerCharacter.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Utils/WildforgeLog.h"

// 移动门控的设计（攻击 / 翻滚 / 滑行共用）：
//   * 事件（OnAttackStarted / OnLandRollStarted / OnSlideStarted…）是**每端各自本地广播**的，
//     所以两端都能就地立即响应，不必等状态复制往返一个 RTT。
//   * 锁分**两种强度**（见头文件里的大段说明）：
//       - 攻击 = 硬锁：StopMovementImmediately + DisableMovement（MOVE_None），真把角色钉住；
//       - 翻滚 / 滑行 = 软锁：只记账 + 复位加速，**不碰移动组件**。
//     这两个能力都不能用硬锁：MOVE_None 会让 PerformMovement 直接 return 并**丢弃根运动**
//     （CharacterMovementComponent.cpp:2716-2734），翻滚的位移就没了、滑行每帧写的速度也会被
//     StopMovementImmediately 当场清掉；而且边跑边滚 / 边跑边滑会被变成急停。
//   * 锁的持有者记成**按来源的集合**（MovementLockHolders），不是布尔、也不是计数：
//       - 布尔无法区分持有者 → 攻击结束会把翻滚的锁一起放掉；
//       - 纯计数无法区分来源 → 重复事件（连击切段重复广播 OnAttackStarted）
//         会累积泄漏，Started/Finished 一旦不成对，锁就永远回不到 0；
//       - 集合两者都解决：重复 Add 幂等，集合非空 = 还有人在锁着，
//         而且日志能直接打出「当前被谁锁着」。
//   * 硬锁另有一个子集 MovementDisablingHolders：只有它清空才恢复移动模式，
//     否则「翻滚中攻击」这种交错会让软锁被硬锁的语义污染。
//   * 持有者被销毁时必须主动解锁（组件被移除后不会再广播 Finished，
//     那份 FName 会永久留在集合里 = 角色永久不能动，详见头文件的前置条件说明）。
//
// 加速的复位也在这里：攻击 / 翻滚 / 滑行期间把最大速度还原到基准值，
// 免得结束后角色带着加速状态继续滑。
//
// ⚠️ 滑行这条链上有一个**顺序依赖**：USlideComponent 是在广播 OnSlideStarted（也就是这里落锁、
// 复位加速）**之后**才快照 / 改写自己的移动参数，所以它拿到的一定是「加速已复位」的状态。
// 反过来（先改参数再广播）会让滑行收尾时把加速期间的旧值当成基线写回去。

// 移动锁的来源标识。定义放在 .cpp 里，头文件只做声明。
const FName APlayerCharacter::MovementLockAttack(TEXT("Attack"));
const FName APlayerCharacter::MovementLockLandRoll(TEXT("LandRoll"));
const FName APlayerCharacter::MovementLockSlide(TEXT("Slide"));

APlayerCharacter::APlayerCharacter() : ABaseCharacter() {
  Inventory = CreateDefaultSubobject<UPlayerInventory>(TEXT("Inventory"));
  Inventory->InitializeContainer(30);

  AttackComponent =
      CreateDefaultSubobject<UAttackComponent>(TEXT("AttackComponent"));
  LandRollComponent =
      CreateDefaultSubobject<ULandRollComponent>(TEXT("LandRollComponent"));

  // 加速与闪现从 ABaseCharacter 拆出来变成独立组件，挂在这里。
  // 组件的复制前提由各自构造函数里的 SetIsReplicatedByDefault(true) 保证，
  // 而「宿主 Actor 必须 bReplicates」由 ABaseCharacter 构造函数保证。
  SprintBoostComponent = CreateDefaultSubobject<USprintBoostComponent>(
      TEXT("SprintBoostComponent"));
  BlinkComponent =
      CreateDefaultSubobject<UBlinkComponent>(TEXT("BlinkComponent"));

  // 滑行（滑铲）组件：位移、时长、冷却、蒙太奇与复制通道都在它自己身上，
  // 这里只负责装配 + 订阅它的生命周期来做移动门控。
  SlideComponent =
      CreateDefaultSubobject<USlideComponent>(TEXT("SlideComponent"));
}

void APlayerCharacter::BeginPlay() {
  Super::BeginPlay();

  const FString Who = GetName();

  // 先全部退订再订阅：蓝图里重复触发 BeginPlay（或角色被重新初始化）时，
  // 不会留下重复绑定。动态多播内部本来也会按对象+函数去重，这里只是显式表达意图。
  if (AttackComponent != nullptr) {
    AttackComponent->OnAttackStarted.RemoveDynamic(
        this, &APlayerCharacter::HandleAttackStarted);
    AttackComponent->OnAttackStarted.AddDynamic(
        this, &APlayerCharacter::HandleAttackStarted);

    AttackComponent->OnAttackFinished.RemoveDynamic(
        this, &APlayerCharacter::HandleAttackFinished);
    AttackComponent->OnAttackFinished.AddDynamic(
        this, &APlayerCharacter::HandleAttackFinished);
  } else {
    WFLOG_WARNING("[移动门控] %s 没有 AttackComponent，攻击期间的移动门控不会生效。",
                  *Who);
  }

  if (LandRollComponent != nullptr) {
    LandRollComponent->OnLandRollStarted.RemoveDynamic(
        this, &APlayerCharacter::HandleLandRollStarted);
    LandRollComponent->OnLandRollStarted.AddDynamic(
        this, &APlayerCharacter::HandleLandRollStarted);

    LandRollComponent->OnLandRollFinished.RemoveDynamic(
        this, &APlayerCharacter::HandleLandRollFinished);
    LandRollComponent->OnLandRollFinished.AddDynamic(
        this, &APlayerCharacter::HandleLandRollFinished);
  } else {
    WFLOG_WARNING("[移动门控] %s 没有 LandRollComponent，翻滚期间的移动门控不会生效。",
                  *Who);
  }

  if (SlideComponent != nullptr) {
    SlideComponent->OnSlideStarted.RemoveDynamic(
        this, &APlayerCharacter::HandleSlideStarted);
    SlideComponent->OnSlideStarted.AddDynamic(
        this, &APlayerCharacter::HandleSlideStarted);

    SlideComponent->OnSlideFinished.RemoveDynamic(
        this, &APlayerCharacter::HandleSlideFinished);
    SlideComponent->OnSlideFinished.AddDynamic(
        this, &APlayerCharacter::HandleSlideFinished);
  } else {
    WFLOG_WARNING("[移动门控] %s 没有 SlideComponent，滑行期间的移动门控不会生效"
                  "（蓝图里创建该组件的节点需要重连到 GetSlideComponent）。",
                  *Who);
  }

  // 拆成组件之后，这两个是「必须有」的：缺了就等于加速 / 闪现能力整个消失，
  // 属于装配错误，要用 WARNING 让它在日志里立刻可见。
  if (SprintBoostComponent == nullptr) {
    WFLOG_WARNING("[加速] %s 没有 SprintBoostComponent，加速能力不可用"
                  "（蓝图里创建该组件的节点需要重连到 GetSprintBoostComponent）。",
                  *Who);
  }
  if (BlinkComponent == nullptr) {
    WFLOG_WARNING("[闪现] %s 没有 BlinkComponent，闪现能力不可用"
                  "（蓝图里创建该组件的节点需要重连到 GetBlinkComponent）。",
                  *Who);
  }

  WFLOG_INFO("[移动门控] %s BeginPlay 完成：本端权威=%d，初始移动模式=%d，"
             "持有者=[%s]（攻击组件=%d 翻滚组件=%d 滑行组件=%d 加速组件=%d 闪现组件=%d）。",
             *Who, HasAuthority() ? 1 : 0,
             // ⚠️ MovementMode 是 TEnumAsByte，直接写在三元表达式里与 MOVE_None
             // 混用会得到 TEnumAsByte 与 EMovementMode 双向可转换的歧义，
             // clang 报 "conditional expression is ambiguous"——显式转 int32 最省事。
             static_cast<int32>(GetCharacterMovement()
                                    ? GetCharacterMovement()->MovementMode.GetValue()
                                    : MOVE_None),
             *DescribeMovementLockHolders(), AttackComponent ? 1 : 0,
             LandRollComponent ? 1 : 0, SlideComponent ? 1 : 0,
             SprintBoostComponent ? 1 : 0, BlinkComponent ? 1 : 0);
}

void APlayerCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // 显式退订：组件与宿主同生共死，这里做的是「不要把已经半销毁的对象留在委托列表里」。
  if (AttackComponent != nullptr) {
    AttackComponent->OnAttackStarted.RemoveDynamic(
        this, &APlayerCharacter::HandleAttackStarted);
    AttackComponent->OnAttackFinished.RemoveDynamic(
        this, &APlayerCharacter::HandleAttackFinished);
  }
  if (LandRollComponent != nullptr) {
    LandRollComponent->OnLandRollStarted.RemoveDynamic(
        this, &APlayerCharacter::HandleLandRollStarted);
    LandRollComponent->OnLandRollFinished.RemoveDynamic(
        this, &APlayerCharacter::HandleLandRollFinished);
  }
  if (SlideComponent != nullptr) {
    SlideComponent->OnSlideStarted.RemoveDynamic(
        this, &APlayerCharacter::HandleSlideStarted);
    SlideComponent->OnSlideFinished.RemoveDynamic(
        this, &APlayerCharacter::HandleSlideFinished);
  }

  Super::EndPlay(EndPlayReason);
}

// ===== 移动门控 =====

FString APlayerCharacter::DescribeMovementLockHolders() const {
  if (MovementLockHolders.Num() == 0) {
    return TEXT("无");
  }

  // TSet 的迭代顺序不保证，排序后输出才能让两次日志可比对
  TArray<FString> Names;
  Names.Reserve(MovementLockHolders.Num());
  for (const FName &Holder : MovementLockHolders) {
    Names.Add(Holder.ToString());
  }
  Names.Sort();
  return FString::Join(Names, TEXT(","));
}

// 硬锁持有者的一行快照。与 DescribeMovementLockHolders 分开：
// 有了软锁之后，「谁持有锁」与「谁要求禁用移动组件」不再是同一件事，
// 排查时这两个集合都要能看到。
FString APlayerCharacter::DescribeMovementDisablingHolders() const {
  if (MovementDisablingHolders.Num() == 0) {
    return TEXT("无");
  }
  TArray<FString> Names;
  Names.Reserve(MovementDisablingHolders.Num());
  for (const FName &Holder : MovementDisablingHolders) {
    Names.Add(Holder.ToString());
  }
  Names.Sort();
  return FString::Join(Names, TEXT(","));
}

// 落锁时的加速复位（软锁与硬锁共用）。
// ResetMaxSpeed 是 BlueprintAuthorityOnly + 内部有非权威端门禁，
// 客户端调用只会在日志里留一条错误，所以这里先自己判权威端。
void APlayerCharacter::ResetSprintBoostOnMovementLock(FName Source) {
  if (SprintBoostComponent == nullptr) {
    return;
  }

  if (!SprintBoostComponent->IsSpeedBoostActive()) {
    if (HasAuthority()) {
      // 服务器侧无论有没有加速都复位一次：它是精确还原基准速度的
      // （未加速时是安全空操作），可以顺手修掉其他系统留下的速度残留。
      SprintBoostComponent->ResetMaxSpeed();
    }
    return;
  }

  if (HasAuthority()) {
    WFLOG_INFO("[移动门控] %s 落下移动锁（来源=%s），同时复位加速状态。",
               *GetName(), *Source.ToString());
    SprintBoostComponent->ResetMaxSpeed();
  } else {
    // 客户端侧不主动复位：本地那份 MaxWalkSpeed 是**从服务器镜像下来的**
    // （SprintBoostComponent 的 ReplicatedMaxWalkSpeed，见 bug-031），
    // 在这里自己改回去只会和镜像值打架（下一帧就被核对写回来，
    // 中途还平白制造一段与服务器不一致的预测）。权威端复位后镜像值会复制下来，
    // 本端自动跟随。这条日志是为了让「加速没被本地中断」这件事在排查时可见。
    WFLOG_INFO("[移动门控] %s 落下移动锁（来源=%s），本端非权威，"
               "加速复位交给服务器（本端会通过复制的镜像值跟随）。",
               *GetName(), *Source.ToString());
  }
}

bool APlayerCharacter::ApplyMovementLock(FName Source, bool bDisableMovement) {
  // 1) 先记账。同一个来源重复落锁是**幂等**的（TSet 的重复 Add 是空操作）——
  //    这一条是必须的：攻击组件在每次连击切段时都会再广播一次 OnAttackStarted，
  //    一次 3 段连击会有 3 个 Started 但只有 1 个 Finished。
  //    如果这里用计数，锁数会停在 2，角色永久不能移动。
  bool bAlreadyAdded = false;
  MovementLockHolders.Add(Source, &bAlreadyAdded);
  if (bAlreadyAdded) {
    WFLOG_INFO("[移动门控] %s 忽略了重复的落锁请求（来源=%s，当前持有者=[%s]）——"
               "同一来源重复落锁是幂等的（强度按第一次落锁时算）。",
               *GetName(), *Source.ToString(), *DescribeMovementLockHolders());
    return false;
  }

  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    // 记账成功了但没有移动组件可用：把这一次的账回退，避免锁永远挂着
    MovementLockHolders.Remove(Source);
    MovementDisablingHolders.Remove(Source);
    WFLOG_WARNING("[移动门控] %s 落锁失败：没有 CharacterMovementComponent"
                  "（来源=%s，已回退记账）。",
                  *GetName(), *Source.ToString());
    return false;
  }

  // 2) 软锁：只记账 + 复位加速，**完全不碰移动组件**。翻滚走这条。
  //
  //    为什么不能顺手 StopMovementImmediately()：那正是「边跑边滚变成急停」的原因。
  //    需要清速度的动作请用组件自己的开关（例如翻滚组件的 bStopMovementOnStart），
  //    不要让「落锁」这个通用机制带上副作用。
  if (!bDisableMovement) {
    ResetSprintBoostOnMovementLock(Source);
    WFLOG_INFO("[移动门控] %s 落下**软锁**（来源=%s，持有者=[%s]）——不修改移动模式与速度，"
               "位移交给蒙太奇根运动 / 蓝图。本端移动是否被禁用=%d。",
               *GetName(), *Source.ToString(), *DescribeMovementLockHolders(),
               bMovementDisabledByLock ? 1 : 0);
    return true;
  }

  // 3) 硬锁：把移动组件真的关掉（MOVE_None）。
  //    判据是「硬锁持有者集合」而不是「全部持有者集合」：翻滚（软）先落锁、
  //    攻击（硬）后落锁时，用 MovementLockHolders.Num() > 1 会挡掉本该执行的禁用。
  MovementDisablingHolders.Add(Source);

  if (bMovementDisabledByLock) {
    // 已经有人把移动组件禁掉了：只记账，不重复操作。
    // 重复 DisableMovement 会把 MOVE_None 记成「之前的移动模式」，之后就恢复不回去了。
    WFLOG_INFO("[移动门控] %s 追加禁止移动请求（来源=%s，持有者=[%s]，禁用者=[%s]，"
               "移动模式=%d）——已是禁止状态，不重复操作。",
               *GetName(), *Source.ToString(), *DescribeMovementLockHolders(),
               *DescribeMovementDisablingHolders(),
               static_cast<int32>(Movement->MovementMode.GetValue()));
    return true;
  }

  // 记下禁用前的移动模式，硬锁全部释放后原样还回去（走路 / 下落 / 飞行各自对应）。
  // 只在 bMovementDisabledByLock 为假时写，所以绝不会把 MOVE_None 记进来。
  MovementModeBeforeLock = Movement->MovementMode;

  ResetSprintBoostOnMovementLock(Source);

  // 禁止移动：MOVE_None 会让移动组件不再积分速度（输入照旧进来但不生效），
  // 但它同时也会**丢弃根运动**（见类注释里的 MOVE_None 坑），
  // 所以硬锁只给不需要位移的动作（攻击）用。
  // 引擎不会替你清速度，所以显式停一次，避免结束后带着旧速度滑出去。
  Movement->StopMovementImmediately();
  Movement->DisableMovement();
  bMovementDisabledByLock = true;

  WFLOG_INFO("[移动门控] %s 落下**硬锁**：开始禁止移动（来源=%s，持有者=[%s]，"
             "禁用者=[%s]，原移动模式=%d，本端权威=%d）。",
             *GetName(), *Source.ToString(), *DescribeMovementLockHolders(),
             *DescribeMovementDisablingHolders(),
             static_cast<int32>(MovementModeBeforeLock.GetValue()),
             HasAuthority() ? 1 : 0);
  return true;
}

bool APlayerCharacter::ReleaseMovementLock(FName Source) {
  // 1) 先解除自己那一份。集合里没有 = Started/Finished 不成对，这属于**状态泄漏**，
  //    必须用 WARNING 暴露（它正是「角色被永久锁住」的直接前兆）。
  if (MovementLockHolders.Remove(Source) == 0) {
    WFLOG_WARNING("[移动门控] %s 收到无配对的解锁请求（来源=%s，当前持有者=[%s]）——"
                  "这个来源并没有持有锁，说明开始/结束通知次数不一致。",
                  *GetName(), *Source.ToString(),
                  *DescribeMovementLockHolders());
    return false;
  }

  // 同时摘掉它的硬锁登记（软锁来源本来就不在这个集合里，Remove 返回 0）
  const bool bWasMovementDisabling = MovementDisablingHolders.Remove(Source) > 0;

  // 2) 还有别的**硬锁**在锁着（例如攻击中翻滚结束）：继续保持禁止移动。
  //    ⚠️ 这里必须判硬锁集合，不能判全部持有者：翻滚是软锁，它还在滚的时候
  //    攻击结束，如果因为「还有人持有锁」而不恢复，角色就会被 MOVE_None 钉住 —— 
  //    软锁被硬锁的语义污染了。
  if (MovementDisablingHolders.Num() > 0) {
    WFLOG_INFO("[移动门控] %s 释放了一份锁（来源=%s，本次是硬锁=%d），"
               "仍有禁止移动的持有者=[%s]（全部持有者=[%s]），继续保持禁止移动。",
               *GetName(), *Source.ToString(), bWasMovementDisabling ? 1 : 0,
               *DescribeMovementDisablingHolders(),
               *DescribeMovementLockHolders());
    return true;
  }

  // 3) 已经没有硬锁持有者了。如果移动不是被这套锁禁掉的，就只更新记账，
  //    绝不去动移动模式（它可能正被死亡 / 布娃娃 / 过场控制着）。
  if (!bMovementDisabledByLock) {
    WFLOG_INFO("[移动门控] %s 释放了一份锁（来源=%s，本次是硬锁=%d），本端移动并未被"
               "这套锁禁用，只更新记账（剩余持有者=[%s]）。",
               *GetName(), *Source.ToString(), bWasMovementDisabling ? 1 : 0,
               *DescribeMovementLockHolders());
    return true;
  }

  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    // 没有移动组件可恢复，但标记必须清掉，否则下一次硬锁会以为「已经禁用着」而不再禁用
    bMovementDisabledByLock = false;
    WFLOG_WARNING("[移动门控] %s 锁已清空（来源=%s），但没有 CharacterMovementComponent，"
                  "无法恢复移动模式（已清掉「被锁禁用」标记）。",
                  *GetName(), *Source.ToString());
    return true;
  }

  // 4) 恢复前确认移动模式确实还停在我们禁用时的 MOVE_None：若已被别的系统改过
  //    （死亡 / 布娃娃 / 过场），说明本次解锁与落锁不配对，硬改回去会跟那个系统抢
  //    控制权——只告警、保持现状，但「被锁禁用」标记要清掉，避免后续硬锁失效。
  bMovementDisabledByLock = false;
  if (Movement->MovementMode != MOVE_None) {
    WFLOG_WARNING("[移动门控] %s 硬锁已全部释放（来源=%s），但移动模式是 %d 而不是 MOVE_None，"
                  "说明状态不配对（或被其他系统改过），保持现状不做恢复。",
                  *GetName(), *Source.ToString(),
                  static_cast<int32>(Movement->MovementMode.GetValue()));
    return true;
  }

  // 恢复禁止前的移动模式。注意 MOVE_Falling 是瞬时状态，落地那一帧移动组件
  // 自己会切回 Walking，所以这里直接还原也不会把人卡在空中。
  EMovementMode RestoreMode = MovementModeBeforeLock.GetValue();
  if (RestoreMode == MOVE_None) {
    // 兜底：记录值本身也是 None（极端情况，说明写记录那条路径被绕过了），退回走路
    WFLOG_WARNING("[移动门控] %s 记录的「禁用前移动模式」是 MOVE_None（异常），"
                  "退回 MOVE_Walking。来源=%s",
                  *GetName(), *Source.ToString());
    RestoreMode = MOVE_Walking;
  }
  Movement->SetMovementMode(RestoreMode);

  // 恢复移动时把残留速度清掉：MOVE_None 期间速度不会被清零，
  // 带着禁止前的速度起身会「滑」一下。
  Movement->StopMovementImmediately();

  WFLOG_INFO("[移动门控] %s 恢复移动（来源=%s，移动模式=%d，本端权威=%d，剩余持有者=[%s]）。",
             *GetName(), *Source.ToString(), static_cast<int32>(RestoreMode),
             HasAuthority() ? 1 : 0, *DescribeMovementLockHolders());
  return true;
}

// ===== 组件事件 -> 移动锁 =====

// 攻击开始 → 硬锁（禁用移动组件）。攻击没有位移，被钉住是预期行为。
void APlayerCharacter::HandleAttackStarted() {
  ApplyMovementLock(MovementLockAttack, /*bDisableMovement=*/true);
}

// 攻击结束 → 释放硬锁
void APlayerCharacter::HandleAttackFinished() {
  ReleaseMovementLock(MovementLockAttack);
}

// 翻滚开始 → **软锁**（不碰移动组件）。
// 翻滚靠蒙太奇根运动 / 蓝图位移，而 MOVE_None 会把根运动直接丢掉
// （CharacterMovementComponent.cpp:2716-2734），所以这里绝不能禁用移动。
void APlayerCharacter::HandleLandRollStarted() {
  ApplyMovementLock(MovementLockLandRoll, /*bDisableMovement=*/false);
}

// 翻滚结束 → 释放软锁
void APlayerCharacter::HandleLandRollFinished() {
  ReleaseMovementLock(MovementLockLandRoll);
}

// 滑行开始 → **软锁**（与翻滚同理，不碰移动组件）。
// 滑行的位移是组件每帧把速度夹到自己的曲线上，硬锁会在落锁那一刻
// StopMovementImmediately 把速度清零、并把移动模式切成 MOVE_None，滑行直接失效。
//
// 顺带把加速复位（软锁路径里做）：这样组件稍后快照 / 改写自己的移动参数时，
// 拿到的基准值是「没被加速抬高过」的那一份。
void APlayerCharacter::HandleSlideStarted() {
  ApplyMovementLock(MovementLockSlide, /*bDisableMovement=*/false);
}

// 滑行结束 → 释放软锁
void APlayerCharacter::HandleSlideFinished() {
  ReleaseMovementLock(MovementLockSlide);
}
