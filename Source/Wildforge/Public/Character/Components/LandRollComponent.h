// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"

#include "LandRollComponent.generated.h"

// 翻滚生命周期通知。
//
// 两个都是**每端各自本地广播**（服务器与每个客户端都会各广播一次），
// 驱动它们的是**本端的表现状态** `bRollPresentationActive`，而不是复制的 `bIsRolling`：
//   * `OnLandRollStarted` 在 `Multicast_PlayLandRollMontage` 里、**表现真的播起来之后**广播；
//   * `OnLandRollFinished` 在收尾（BlendOut / 播完 / 超时 / 强制结束）时广播。
//   两者严格配对：表现没播起来就一个都不广播（否则会出现「只落锁、没人解锁」）。
//
// ⚠️ 为什么不能拿复制的 `bIsRolling` 当判据：它是 `COND_OwnerOnly`，
// 模拟代理（其他玩家）永远收不到、永远是 false——用它做判断的话，
// 除「拥有者自己」以外的端都会只 Started 不 Finished。
//
// 订阅者（APlayerCharacter 的移动门控）只认这两个事件，
// 不需要知道蒙太奇、Section、RootMotion 这些细节。
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLandRollStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLandRollFinished);

/**
 * 翻滚组件（服务器权威）。
 *
 * ## 权威边界一览
 *
 * | 内容 | 权威端 | 客户端 | 通道 |
 * |---|---|---|---|
 * | 翻滚判定与状态推进 `LandRoll()` | 服务器 | ✗ 调不动（Absorbed） | `Server_LandRoll` |
 * | 冷却 / 重入 / 总开关判定 | 服务器 | ✗ | 在 `LandRoll()` 内部完成 |
 * | 调参（`LandRollCooldown`/播速/BlendOut 解锁…） | 服务器读 ini 后写入 | 只读（复制下来） | 类默认值兜底 + `COND_InitialOnly`（冷却项沿用原有通道） |
 * | 状态 `bIsRolling` / `LastLandRollTime` | 服务器写 | 只读（复制下来） | `DOREPLIFETIME_CONDITION(COND_OwnerOnly)` |
 * | 蒙太奇播放 | 服务器发起 | 跟随（但不写玩法状态） | `Multicast_PlayLandRollMontage(段名)` |
 * | 生命周期广播 `OnLandRollStarted/Finished` | 每端各自本地 | 每端各自本地（不复制） | `bRollPresentationActive` |
 * | 特效 `PlayLandRollEffects()` | 服务器发起 | 跟随 | `Multicast_PlayLandRollEffects` |
 *
 * ### 「别人能不能看到你翻滚」= 两件独立的事
 *
 *   * **动画**：能。靠的是 `Multicast_PlayLandRollMontage`（`NetMulticast, Reliable`），
 *     每个端本地 `Montage_Play` —— 引擎默认**不**复制蒙太奇（`ACharacter` 只复制
 *     RootMotion 那一段 `FRepRootMotionMontage`），所以这是动画能被别人看到的唯一原因。
 *     它和下面那些属性的 `condition` **毫无关系**。
 *   * **状态属性**：`COND_OwnerOnly` = 只发给**拥有者连接**。也就是说
 *     `bIsRolling` / `LastLandRollTime` / `LandRollCooldown` / `LandRollCount` 是
 *     「自己看得到、别人看不到」（`RepLayout.h:149-152` 的 `ConditionMap`：
 *     `COND_OwnerOnly = bIsOwner`，而 `DataChannel.cpp:3809` 会把非拥有者连接的
 *     角色临时降级成 SimulatedProxy）。想让所有人都读到这些值，就必须**不带
 *     condition**（`DOREPLIFETIME`，等价 `COND_None`）。
 *     所以别指望别人用 `bIsRolling` 判断你在不在翻滚——要用就改成全发。
 *
 * ⚠️ 只有 `bIsRolling` / `LastLandRollTime` / `LandRollCooldown` / `LandRollCount`
 * 是复制属性；**`ActiveLandRollMontage` 不是纯复制的**——它在每个端由
 * `PlayLandRollMontageInternal` 各写一份（客户端要靠它判断「结束的是不是我在播的那一段」，
 * 等属性复制再写会与 Multicast 乱序）。
 *
 * ⚠️ 生命周期广播同样**不依赖**复制的 `bIsRolling`，而是各端自己的
 * `bRollPresentationActive`（见下方成员说明）。原因就是上面那条 `COND_OwnerOnly`：
 * 模拟代理读不到 `bIsRolling`，靠它判断会让别人的客户端只落锁、不解锁。
 *
 * 判断规则：**会改变玩法状态的一律权威端执行**；纯表现（动画播放、特效）
 * 用 Multicast 同步到所有端。权威函数都带 `BlueprintAuthorityOnly`，并且内部还有一道
 * `IsAuthoritativeForActorComponent(this)` 开发期门禁，让客户端误用立刻在日志里可见，
 * 而不是被引擎静默丢弃（`callspace = Absorbed`，单机看不出来、联机才失效）。
 *
 * ## 三条容易搞错的地方
 *
 * 1. **`OnMontageEnded` 会为「任何」蒙太奇触发**：别人的蒙太奇（受击、死亡、攀爬）
 *    播完也会进来。所以回调里必须先用 `ActiveLandRollMontage` 确认
 *    「结束的是不是我在播的那一个」，否则翻滚状态会被无关动画清掉。
 *    （`bIsRolling` 一旦被清，服务器就会允许立刻再滚一次 = 冷却形同虚设。）
 * 2. **动画不保证一定结束**：蒙太奇被打断、AnimInstance 被换掉、动画蓝图出错，
 *    都可能让 `OnMontageEnded` 永远不来。没有兜底的话 `bIsRolling` 会永久为 true，
 *    角色再也滚不了。所以 `OpenRollTimeout()` 会按蒙太奇时长挂一个一次性定时器。
 * 3. **客户端不能写玩法状态**：`LandRoll()` 是权威函数，客户端只能发
 *    `Server_LandRoll`。蒙太奇播放在客户端走的是同一个 RPC 的 Multicast 分支，
 *    只播动画、不碰 `bIsRolling`。
 *
 * ⚠️ `UActorComponent` 上没有 `HasAuthority()`（那是 `AActor` 的方法，直接写会 C3861），
 * 组件里一律用 `Utils/WildforgeAuthority.h` 的 `IsAuthoritativeForActorComponent(this)`。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API ULandRollComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  ULandRollComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  virtual void BeginPlay() override;

  // 销毁时清掉兜底定时器与动画委托：next-tick 之外的回调也可能打到半销毁对象上
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  // ===== 可配置参数（类默认值 = 基线 + 兜底；带 `Replicated` 的可被 ini 覆盖）=====
  // 带 `Replicated`（`COND_InitialOnly`，出生束里带一次）的那几项会被
  // ULandRollComponentSettings 的 ini 覆盖：权威端在 BeginPlay 写入，客户端靠复制拿到
  // 同一份值（客户端不读 ini）。没勾 override 就是这里的类默认值。
  // ⚠️ 客户端不要在本端写这些字段。

  // 翻滚蒙太奇。**留空 = 翻滚状态仍然会推进，只是没有动画**（会记一条 WARNING）。
  // 内容仓库里的资源是 /Game/Characters/Man/Animations/Montage/LandRollMontage。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandRoll|Anim")
  TObjectPtr<UAnimMontage> LandRollMontage;

  // 蒙太奇播放速率倍率（1 = 原速）。翻滚通常要比走跑快一点才「利落」，
  // 默认 1.5 是原先就有的取值。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "LandRoll|Anim",
            meta = (ClampMin = "0.01", UIMin = "0.01"))
  float MontagePlayRate = 1.5f;

  // 只在指定 Section 播放（`NAME_None` = 从第 0 段 / 蒙太奇开头播）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandRoll|Anim")
  FName LandRollSectionName = NAME_None;

  // 混合淡出（BlendOut）时就结束翻滚状态（默认开）。
  //
  // 为什么默认开：翻滚的收尾动作（起身）通常占了蒙太奇的最后一小段，
  // 等 OnMontageEnded 才解锁会让玩家在「动画看着已经站好了」之后还被锁半秒。
  // 在 BlendOut 解锁能让移动立刻接上。
  // 关掉它就只在蒙太奇真正播完 / 被打断时才解锁（更保守，适合起身动作不可打断的设计）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "LandRoll|Advanced")
  bool bFinishOnBlendOut = true;

  // ===== 玩法参数（服务器权威判定用；带 `Replicated` 的同样可被 ini 覆盖）=====

  // 能不能翻滚的总开关（眩晕 / 缴械 / 死亡等状态把它关掉）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandRoll")
  bool bCanLandRoll = true;

  // 翻滚后是否把速度清零。
  // 只有在翻滚靠**自己位移**（比如蓝图里给一段位移曲线 / Launch）而不是 RootMotion
  // 驱动时才需要打开；用 RootMotion 的话速度本来就由动画接管，清零反而会让动作发飘。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "LandRoll|Advanced")
  bool bStopMovementOnStart = false;

  // ===== 状态（服务器写、拥有者客户端只读）=====

  // 是否正在翻滚（客户端用它做 UI / 输入门控，不参与玩法判定）
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  bool bIsRolling = false;

  // 上一次翻滚开始的世界时间（秒）。只在服务器上读写（冷却判定依据），
  // 复制给客户端只是为了调试与 UI 显示。
  //
  // ⚠️ 初值是 -1000 而不是 0：用 0 会让「刚 BeginPlay、游戏时间还没到冷却时长」
  // 的那几秒里翻滚被冷却误挡（`Now - 0 < Cooldown`）。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  float LastLandRollTime = -1000.0f;

  // 翻滚冷却（秒）：两次翻滚开始时间的最小间隔。0 = 不限制。
  //
  // 它既是配置（EditAnywhere，设计器里改）**也**在复制列表里，这是有意的：
  // 客户端要算「冷却还剩几秒」的 UI，必须拿到和服务器同一份数值与同一个
  // `LastLandRollTime`（用客户端自己的配置会算出不一致的进度）。
  // 玩法判定始终在服务器，客户端改这个值只影响它自己的本地副本。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated,
            Category = "LandRoll",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float LandRollCooldown = 1.0f;

  // 成功翻滚的累计次数（调试 / UI 用）
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  int32 LandRollCount = 0;

  // 当前这一段实际在播的蒙太奇。
  //
  // 它**在每个端各写一份**（`PlayLandRollMontageInternal` 里写），同时也参与复制：
  // 复制的意义是让「还没轮到执行 Multicast 的端」也能看到正确的状态用于 UI / 调试；
  // 判断「结束的是不是我在播的那段」则靠各端自己写下的这一份，
  // 不依赖复制时序（RPC 与属性复制谁先到不保证）。
  //
  // 为什么不直接在回调里拿 `Montage` 参数与配置项 `LandRollMontage` 比对：
  // 配置可能被热改、动画蓝图也可能换了实例，直接记「在播什么」最稳。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  TObjectPtr<UAnimMontage> ActiveLandRollMontage;

  // ===== 通知 =====
  // 两段式生命周期：OnLandRollStarted（翻滚真的要开始了）/ OnLandRollFinished（结束）。
  // 订阅者（APlayerCharacter 的移动门控）只认这两个事件。

  UPROPERTY(BlueprintAssignable, Category = "LandRoll")
  FOnLandRollStarted OnLandRollStarted;

  UPROPERTY(BlueprintAssignable, Category = "LandRoll")
  FOnLandRollFinished OnLandRollFinished;

  // ===== 查询（纯读，任何端都能安全调用）=====

  // 翻滚状态的一行快照，专供日志 / 调试用（每个判定点都会打它）
  UFUNCTION(BlueprintPure, Category = "LandRoll", meta = (BlueprintThreadSafe))
  FString GetLandRollDebugString() const;

  // 冷却是否已经走完
  UFUNCTION(BlueprintPure, Category = "LandRoll", meta = (BlueprintThreadSafe))
  bool IsLandRollReady() const;

  // 剩余冷却时间（秒），0 = 可以滚
  UFUNCTION(BlueprintPure, Category = "LandRoll", meta = (BlueprintThreadSafe))
  float GetLandRollCooldownRemaining() const;

  // 本端是否正在播放翻滚表现（**不复制**，各端自己维护）。
  //
  // ⚠️ 与复制的 `bIsRolling` 的区别：`bIsRolling` 是 `COND_OwnerOnly`，
  // 只有拥有者客户端读得到；本函数在**任何端**都反映「我这端正在不在演翻滚」。
  // 动画蓝图 / UI 想在所有端都正确工作，请用这个（而不是 bIsRolling）。
  UFUNCTION(BlueprintPure, Category = "LandRoll", meta = (BlueprintThreadSafe))
  bool IsRollPresentationActive() const { return bRollPresentationActive; }

  // ===== 表现同步（服务器 -> 所有端）=====
  // 引擎默认**不复制蒙太奇播放**（`ACharacter` 只复制 RootMotion 那一段，
  // `FRepRootMotionMontage`），所以想让客户端也看到翻滚动画必须显式 Multicast。
  //
  // 参数直接把「播哪一段」带过去，客户端**不读**复制的状态来决定播什么：
  // RPC 与属性复制谁先到不保证，靠读状态会让客户端偶尔播错。
  //
  // 本函数负责广播 `OnLandRollStarted`（表现真的播起来之后），是生命周期广播的唯一入口。
  //
  // ⚠️ 客户端自己调这个 RPC 只会**在本端本地执行一次**，既不发服务器也不广播给别人
  // （引擎行为：`AActor::GetFunctionCallspace` 对 Multicast 在客户端返回 `Local`，
  // 见 `Actor.cpp:5500-5519`）。所以本函数必须做到「在任何端本地执行都安全」：
  // 只播动画 + 维护本端表现标志，不写任何玩法状态。

  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "LandRoll|RPC")
  void Multicast_PlayLandRollMontage(FName InSectionName);

  // 在所有端播放翻滚表现（特效 / 音效 / 镜头）。蓝图覆写 `PlayLandRollEffects` 即可。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "LandRoll|RPC")
  void Multicast_PlayLandRollEffects();

  // 蓝图覆写的表现入口（纯表现，任何端都可以本地调）。C++ 默认什么都不做。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "LandRoll")
  void PlayLandRollEffects();

  // ===== 服务器权威函数（BlueprintAuthorityOnly + 内部开发期门禁）=====
  // 客户端直接调用会被引擎静默丢弃（callspace = Absorbed），请改调 `Server_LandRoll`。

  // 发起一次翻滚。
  // 依次校验：总开关 → 重入（已在翻滚）→ 冷却 → 蒙太奇配置。
  // 返回是否**真的开始了**本次翻滚（被拒时记一条 WFLOG_WARNING / WFLOG_INFO 说明原因）。
  //
  // ⚠️ 蒙太奇没播起来（没配 / 没有 AnimInstance / Montage_Play 被拒）时返回 false，
  // 且权威端会**立刻收尾**（收尾路径见 Multicast_PlayLandRollMontage 的实现）：
  // 不落锁、不广播 Started，同时把 bIsRolling 复位，避免「状态说在翻滚、
  // 却没有任何东西会来结束它」而永久卡住冷却与重入判定。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "LandRoll")
  bool LandRoll();

  // 清除冷却（服务器侧）：复活 / 传送 / 调试用
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "LandRoll")
  void ResetLandRollCooldown();

  // 强制结束当前翻滚（服务器侧）：被受击 / 眩晕打断时由服务器蓝图调。
  // 会停掉蒙太奇、复位状态并广播 OnLandRollFinished（订阅者据此恢复移动）。
  // 未在翻滚时调用是安全的空操作。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "LandRoll")
  void ForceFinishLandRoll();

  // ===== 客户端 -> 服务器：翻滚请求 =====
  // 输入绑定通常写在蓝图里（Enhanced Input），C++ 侧不重复绑定。
  // `_Validate` 只做廉价检查（这里没有参数，恒为 true）；
  // 真正的重入 / 冷却 / 总开关判定全部在 `LandRoll()` 内部。

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "LandRoll|RPC")
  void Server_LandRoll();

protected:
  // 读取 ULandRollComponentSettings 的 ini 覆盖并写进本组件。
  // **只在权威端执行**（客户端那份值由 `COND_InitialOnly` 属性复制下来）。
  // 无覆盖时是安全空操作。
  void ApplyGameplaySettingsOverrides();

  // 蒙太奇结束回调。
  //
  // 所有端都会触发（蒙太奇被 Multicast 同步到每个端），但状态收尾的语义在服务器与
  // 客户端都成立，所以这里不加权威门禁：权威端写 bIsRolling，客户端只清本端表现标志。
  // **只认 `ActiveLandRollMontage`**：别的蒙太奇结束不该影响翻滚状态（见类注释 1）。
  UFUNCTION()
  void OnLandRollMontageEnded(UAnimMontage *Montage, bool bInterrupted);

  // 混合淡出开始时的回调（可选提前解锁，见 bFinishOnBlendOut）。
  // 与 OnLandRollMontageEnded 分开接，是为了让「提前解锁」与「真正结束」两条路径
  // 各自有明确的语义，而不是在同一个回调里靠标志猜。
  UFUNCTION()
  void OnLandRollMontageBlendingOut(UAnimMontage *Montage, bool bInterrupted);

  // 兜底：动画异常（蒙太奇没触发 OnMontageEnded）时强制收尾，
  // 避免本端永久停在「表现中」/ bIsRolling=true 而再也滚不了。
  // 时长按蒙太奇剩余时间估算。
  void HandleLandRollTimeout();

  // 无门禁内核：服务器与客户端都走它（客户端只播动画、不写任何玩法状态）。
  // 权威入口与 Multicast 都转发到这里，避免逻辑写两份。
  // 返回**是否真的播起来了**——调用方据此决定要不要广播 OnLandRollStarted
  // 与挂兜底定时器（没播起来就什么都不做，否则会「只落锁、没人解锁」）。
  bool PlayLandRollMontageInternal(FName InSectionName);

  // 整段翻滚收尾：清定时器 + 复位表现标志与（权威端的）玩法状态。
  // `OnLandRollFinished` 只在**本端确实广播过 Started**（原本 bRollPresentationActive
  // 为真）时广播，保证 Started / Finished 严格配对。
  // `bStopMontage` = 是否要主动停掉蒙太奇（超时 / 强制结束用；正常结束传 false）。
  void FinishLandRoll(const TCHAR *Reason, bool bStopMontage);

  // 按蒙太奇时长挂一次性兜底定时器（动画没结束时强制收尾）
  void OpenRollTimeout();
  // 蒙太奇时长（秒）；取不到时返回 1.0 作为兜底
  float GetMontagePlayLength() const;

  // 是否已经订阅过动画回调（避免每次翻滚都 AddDynamic 一遍）
  bool bMontageCallbacksBound = false;

  // 我们正在为「混合淡出提前解锁」而收尾，避免随后到来的 OnMontageEnded 再收一次尾
  bool bFinishingFromBlendOut = false;

  // 本端「翻滚表现正在播放」标志。**不复制，每个端各写一份**。
  //
  // 它就是生命周期广播的唯一判据（Started 与 Finished 严格由它配对）：
  //   * 由 `Multicast_PlayLandRollMontage` 在表现**真的播起来之后**置 true 并广播 Started；
  //   * 由 `FinishLandRoll` 置 false 并广播 Finished。
  //   表现没播起来（没配蒙太奇 / 没有 AnimInstance / Montage_Play 被拒）时保持 false，
  //   于是一个事件都不广播 —— 这样订阅者不会「只落锁、没人解锁」。
  //
  // 为什么需要这个额外标志，而不直接用复制的 `bIsRolling`：
  // `bIsRolling` 是 `COND_OwnerOnly`，模拟代理（其他玩家）永远收不到、永远是 false。
  // 而生命周期广播必须**在所有端**都成立（移动门控是各端本地的），
  // 所以判据只能是各端自己的表现状态。
  bool bRollPresentationActive = false;

  // 翻滚超时兜底定时器
  FTimerHandle RollTimeoutHandle;
};
