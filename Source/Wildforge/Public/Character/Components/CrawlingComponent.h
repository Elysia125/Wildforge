// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"

#include "CrawlingComponent.generated.h"

class UAnimInstance;
class USprintBoostComponent;

// 一次趴下 / 起立**过渡动作**的类型。
//
// 它由服务器在起手时**选定一次**，再随 `Multicast_PlayCrawlMontage` 发给所有端：
// 客户端**不读**任何权威状态自己猜该播哪一段（RPC 与属性复制谁先到不保证，
// 靠读状态会让客户端偶尔播错动画）。
UENUM(BlueprintType)
enum class ECrawlTransition : uint8 {
  // 没有正在进行的过渡动作
  None UMETA(DisplayName = "无"),
  // 趴下（站立起手）：用 `ProneFromStandMontage`
  EnterProneFromStand UMETA(DisplayName = "趴下（站立起手）"),
  // 趴下（奔跑起手）：用 `ProneFromRunMontage`（没配时回退到站立那一段）
  EnterProneFromRun UMETA(DisplayName = "趴下（奔跑起手）"),
  // 回滚站立：用 `ProneToStandMontage`
  ExitProneToStand UMETA(DisplayName = "回滚站立（起身）"),
};

// 过渡动作的生命周期通知。两个都是**每端各自本地广播**（服务器与每个客户端各一次），
// 驱动它们的是**本端的表现状态** `bCrawlPresentationActive`，而不是复制的 `bIsProne`：
//   * `OnCrawlTransitionStarted` 在 `Multicast_PlayCrawlMontage` 里、表现**真的播起来之后**广播；
//   * `OnCrawlTransitionFinished` 在收尾（BlendOut / 播完 / 超时 / 强制结束）时广播，
//     并把这一次的动作类型带上，订阅者不必回头查状态就能知道是「趴下」还是「起立」。
//   两者严格配对：表现没播起来就一个都不广播（否则订阅者会「只落锁、没人解锁」）。
//
// 订阅者（`APlayerCharacter` 的移动门控）只认这两个事件，不需要知道蒙太奇 / Section 细节。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCrawlTransitionStarted,
                                            ECrawlTransition, Transition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCrawlTransitionFinished,
                                            ECrawlTransition, Transition);

/**
 * 趴下（爬行）组件（服务器权威）。
 *
 * ## 它管什么
 *
 * 「趴下」不是一个瞬时动作，而是**一个持续姿态 + 两次过渡动作**：
 *
 *   * 过渡动作（有动画、占用角色）：站立趴下 / 奔跑趴下 / 回滚站立；
 *   * 持续姿态 `bIsProne`：趴着爬行，最大速度降到 `ProneMaxWalkSpeed`。
 *
 * | 蒙太奇 | 何时播 | 可以留空吗 |
 * |---|---|---|
 * | `ProneFromStandMontage`（站着趴下） | 站立状态进入趴下 | 建议配上 |
 * | `ProneFromRunMontage`（奔跑趴下） | 水平速度 >= `RunTransitionSpeedThreshold` 时优先用它 | **可空**（留空 = 奔跑中趴下也播站立那一段） |
 * | `ProneToStandMontage`（回滚站立） | 从趴下回到站立 | 建议配上 |
 *
 * 蒙太奇为空是**合法状态**（与 `USlideComponent` / `USprintBoostComponent` 一致）：
 * 姿态与速度照常切换，只是没有动画，日志记 INFO（不是 WARNING——空配置是设计内状态）。
 * 这里与 `ULandRollComponent` 的取舍**刻意不同**：翻滚是瞬时动作，没有动画就等于什么都
 * 没发生，所以那份实现会回滚玩法状态；而趴下的姿态本身就是目的（动画蓝图 / UI 都要读
 * `bIsProne`），没有动画也应该趴下去。
 *
 * ## 权威边界一览
 *
 * | 内容 | 权威端 | 客户端 | 通道 |
 * |---|---|---|---|
 * | 姿态与过渡判定 `EnterProne()` / `ExitProne()` | 服务器 | ✗ 调不动（Absorbed） | `Server_EnterProne` / `Server_ExitProne` / `Server_ToggleProne` |
 * | 冷却 / 重入 / 总开关判定 | 服务器 | ✗ | 在上述函数内部完成 |
 * | 姿态 `bIsProne` | 服务器写 | 只读（复制下来） | `DOREPLIFETIME`（**不带 condition**，见下） |
 * | `bCrawlTransitionActive` / `LastCrawlTransitionTime` / `CrawlCooldown` / `CrawlTransitionCount` / `ActiveCrawlMontage` | 服务器写 | 只读（复制下来） | `DOREPLIFETIME_CONDITION(COND_OwnerOnly)` |
 * | 爬行速度 | 服务器写 | 跟随（镜像） | `USprintBoostComponent::SetBaseMaxSpeed`（见下） |
 * | 调参（`ProneMaxWalkSpeed`/阈值/播速…） | 服务器读 ini 后写入 | 只读（复制下来） | 类默认值兜底 + `COND_InitialOnly`（`CrawlCooldown` 沿用原有通道） |
 * | 蒙太奇播放 | 服务器发起 | 跟随（但不写玩法状态） | `Multicast_PlayCrawlMontage(动作类型)` |
 * | 生命周期广播 `OnCrawlTransitionStarted/Finished` | 每端各自本地 | 每端各自本地（不复制） | 本端 `bCrawlPresentationActive` |
 * | 特效 `PlayCrawlEffects()` | 服务器发起 | 跟随 | `Multicast_PlayCrawlEffects` |
 *
 * ### `bIsProne` 为什么不带 condition（和翻滚那几个属性不一样）
 *
 * 选 condition 的规则是先问「**这个值最终是谁消费的**」（见 bug-030 的教训）：
 * 拥有者客户端的 UI → `COND_OwnerOnly`；其他玩家的表现 → `COND_SimulatedOnly`；
 * **两端都要 → 不带 condition**。趴下姿态属于第三类：过渡蒙太奇播完之后，**每个端**的
 * 动画蓝图都要靠 `bIsProne` 把角色保持在趴下姿态（否则 BlendOut 一结束就弹回站立），
 * 所以它必须发到所有端。它是 1 个 bit，代价可以忽略。
 * 其余几个（冷却 / 计数 / 调试时间戳 / 在播的蒙太奇）只给拥有者 UI 与排查用，沿用 OwnerOnly。
 *
 * ### ⚠️ `bIsProne` 在**过渡期间也是 true**
 *
 * 进入趴下的那一刻就置 true（速度、动画蓝图、UI 都按「已经在趴下流程里」处理），
 * 一直到**起立过渡结束**才置 false。也就是说「趴着」= `bIsProne`，
 * 「正在播过渡动作」= `bCrawlTransitionActive`（两个标志各管一件事，不合并——
 * 合并就意味着在两者之间做取舍，翻滚组件合并过一次，代价见 bug-030）。
 *
 * ## 速度：为什么改速度要绕到 `USprintBoostComponent`
 *
 * 趴着要变慢，但本项目的 `MaxWalkSpeed` **由 `USprintBoostComponent` 独占**
 * （它把 MaxWalkSpeed / MaxWalkSpeedCrouched 当成基准值快照与还原的资源，见 bug-027、
 * bug-031 与 cerebrum 的 Do-Not-Repeat）：第二个组件也去「快照 → 改写 → 还原」，
 * 就会出现「另一方中途复位、这一方收尾时把过期值写回去」= 角色永久带着错误速度。
 *
 * 所以本组件改速度**一律走 `USprintBoostComponent::SetBaseMaxSpeed()`**：
 *
 *   * 进入趴下：先 `ResetMaxSpeed()` 收掉正在跑的加速（趴着冲刺没有意义，而且
 *     `SetBaseMaxSpeed` 在加速期间会被拒），再 `SetBaseMaxSpeed(ProneMaxWalkSpeed)`；
 *   * 起立结束：`SetBaseMaxSpeed(趴下前的基准速度)` 精确还原。
 *     ⚠️ 基准速度必须配 `bProneBaseSpeedCaptured` 一起用：**0 是「角色不能动」的合法
 *     速度值**，光看数值分不出「还没抓过」与「真的是 0」（bug-027 就是这么把角色钉死的）。
 *
 * 顺带的好处：`USprintBoostComponent` 会把权威速度**镜像**给拥有者客户端
 * （`ReplicatedMaxWalkSpeed`——UE 5.7 的移动组件根本不复制移动参数，见 bug-031），
 * 于是客户端预测也跟着一起变慢，不会橡皮筋。**宿主没有加速组件时**只能直接写
 * `MaxWalkSpeed`（记一条 INFO）：那时没有独占冲突，但客户端预测确实不会跟随，属于装配降级。
 *
 * ## 三条容易搞错的地方（与翻滚同源）
 *
 * 1. **`OnMontageEnded` 会为「任何」蒙太奇触发**：别人的蒙太奇（受击、死亡、翻滚）播完
 *    也会进来。回调里必须先用 `ActiveCrawlMontage` 确认「结束的是不是我在播的那一个」，
 *    否则趴下状态会被无关动画清掉。
 * 2. **动画不保证一定结束**：蒙太奇被打断、AnimInstance 被换掉、动画蓝图出错，
 *    都可能让回调永远不来。所以 `OpenCrawlTimeout()` 按蒙太奇时长挂一次性兜底定时器。
 *    ⚠️ 与翻滚不同，它**在每个端都挂**：每个端都有自己的一份表现状态要收尾
 *    （「趴着但谁也解锁不了」比「多等一秒」糟糕得多）。
 * 3. **客户端不能写玩法状态**：`EnterProne()` / `ExitProne()` 都是权威函数，客户端只能发
 *    `Server_*`。蒙太奇播放在客户端走 Multicast 分支，只播动画、不碰 `bIsProne`。
 *
 * ⚠️ `UActorComponent` 上没有 `HasAuthority()`（那是 `AActor` 的方法，直接写会 C3861），
 * 组件里一律用 `Utils/WildforgeAuthority.h` 的 `IsAuthoritativeForActorComponent(this)`。
 *
 * ## 与翻滚 / 滑行 / 攻击的互斥
 *
 * **本组件不做跨能力的互斥判定**（保持组件解耦，与项目里其他能力组件一致）：
 * 需要「翻滚中不能趴下」这类规则时，由外部（蓝图 / 角色类）用 `bCanCrawl` 总开关或
 * 移动锁协调。各组件自己的重入判定只保证「同一个能力不会叠两次」。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API UCrawlingComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  UCrawlingComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  virtual void BeginPlay() override;

  // 销毁时清兜底定时器 + 退订动画委托 + 还原基准速度
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  // ===== 可配置参数（类默认值 = 基线 + 兜底；带 `Replicated` 的可被 ini 覆盖）=====
  // 带 `Replicated`（`COND_InitialOnly`，出生束里带一次）的那几项会被
  // UCrawlingComponentSettings 的 ini 覆盖：权威端在 BeginPlay 写入，客户端靠复制拿到
  // 同一份值（客户端不读 ini）。没勾 override 就是这里的类默认值。
  // ⚠️ 客户端不要在本端写这些字段。

  // 站着趴下：从站立进入趴下时播的蒙太奇
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling|Anim")
  TObjectPtr<UAnimMontage> ProneFromStandMontage;

  // 奔跑趴下（**可选**）：水平速度 >= `RunTransitionSpeedThreshold` 时优先用它。
  // 留空 = 奔跑中趴下也播 `ProneFromStandMontage`（设计内状态，记 INFO）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling|Anim")
  TObjectPtr<UAnimMontage> ProneFromRunMontage;

  // 回滚站立（起身）：从趴下回到站立时播的蒙太奇
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling|Anim")
  TObjectPtr<UAnimMontage> ProneToStandMontage;

  // 蒙太奇播放速率倍率（1 = 原速）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Crawling|Anim",
            meta = (ClampMin = "0.01", UIMin = "0.01"))
  float MontagePlayRate = 1.0f;

  // 三个蒙太奇各自的起始 Section（`NAME_None` = 从第 0 段 / 蒙太奇开头播）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling|Anim")
  FName ProneFromStandSectionName = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling|Anim")
  FName ProneFromRunSectionName = NAME_None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling|Anim")
  FName ProneToStandSectionName = NAME_None;

  // 混合淡出（BlendOut）时就结束过渡状态（默认开）。
  //
  // 为什么默认开：过渡动作的收尾通常已经「到位」了（人已经躺下 / 已经站起来），
  // 等 `OnMontageEnded` 才解锁会让玩家在「动画看着已经结束了」之后还被锁半秒。
  // 关掉它就只在蒙太奇真正播完 / 被打断时才收尾（更保守：适合起身动作不可打断的设计）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated,
            Category = "Crawling|Advanced")
  bool bFinishOnBlendOut = true;

  // 判定「奔跑中趴下」的水平速度阈值（cm/s）。只有配了 `ProneFromRunMontage` 才参与判定。
  //
  // ⚠️ 读的是**服务器上的 `Velocity`**：自主代理的速度由客户端 move 包驱动，
  // 服务器上也是真值；**不要**用 last input vector —— 它只由客户端累加，
  // 服务器上恒为零向量（见 bug-026）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated,
            Category = "Crawling|Advanced", meta = (ClampMin = "0", UIMin = "0"))
  float RunTransitionSpeedThreshold = 300.0f;

  // ===== 玩法参数（服务器权威判定用，客户端改了也没用）=====

  // 能不能发起趴下 / 起立的总开关（眩晕 / 中断等状态把它关掉）。
  //
  // ⚠️ 它**不会**把已经趴着的角色拉起来，也不会自己恢复：关掉它的那一方负责重新打开，
  // 否则角色会一直趴着（这种「永久趴着」要复位就用 `ForceEndProne()`）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crawling")
  bool bCanCrawl = true;

  // 趴下时的爬行最大速度（cm/s）：进入趴下时写入，起立结束时精确还原成趴下前的值。
  // 必须 > 0：0 是「角色不能动」的合法速度值，写进去会把角色钉在原地（见 bug-027）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Crawling",
            meta = (ClampMin = "1", UIMin = "1"))
  float ProneMaxWalkSpeed = 150.0f;

  // 两次过渡动作之间的最小间隔（秒）。0 = 不限制。
  //
  // 它既是配置（EditAnywhere）**也**在复制列表里（与翻滚同样的理由）：
  // 客户端要算「还剩几秒」的 UI，必须拿到和服务器同一份数值与同一个
  // `LastCrawlTransitionTime`。玩法判定始终在服务器，客户端改它只影响本地副本。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Crawling",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float CrawlCooldown = 0.5f;

  // ===== 状态（服务器写、客户端只读）=====

  // 是否处于趴下姿态（**包含「正在趴下 / 正在起立」这两段过渡**，见类注释）。
  // 所有端都读得到：动画蓝图靠它保持趴下姿态，UI 靠它显示状态。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Crawling")
  bool bIsProne = false;

  // 是否正在播一次过渡动作（趴下或起立都算），期间不接受新的趴下 / 起立请求。
  // 只发给拥有者连接（它才是会拿它做输入门控 / UI 的那一端）。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Crawling")
  bool bCrawlTransitionActive = false;

  // 上一次过渡开始的**服务器**世界时间（秒）。只在服务器上读写（冷却判定依据），
  // 复制给客户端只是为了调试与 UI 显示。
  //
  // ⚠️ 两个坑：① 初值是 -1000 而不是 0，否则「刚 BeginPlay、游戏时间还没到冷却时长」
  // 的那几秒里会被冷却误挡（`Now - 0 < Cooldown`）；② 客户端**不要**拿它算进度，
  // 两端 World 时间起点不同（见滑行组件的同类说明）。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Crawling")
  float LastCrawlTransitionTime = -1000.0f;

  // 过渡动作的累计次数（调试 / UI 用）
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Crawling")
  int32 CrawlTransitionCount = 0;

  // 本段实际在播的过渡蒙太奇。
  //
  // 它在**每个端各写一份**（`PlayCrawlMontageInternal` 里写），同时也参与复制：
  // 复制的意义是让「还没轮到执行 Multicast 的端 / 想调试的人」看到正确的状态；
  // 判断「结束的是不是我在播的那段」则靠各端自己写下的这一份，不依赖复制时序
  // （RPC 与属性复制谁先到不保证）。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Crawling")
  TObjectPtr<UAnimMontage> ActiveCrawlMontage;

  // ===== 通知 =====

  UPROPERTY(BlueprintAssignable, Category = "Crawling")
  FOnCrawlTransitionStarted OnCrawlTransitionStarted;

  UPROPERTY(BlueprintAssignable, Category = "Crawling")
  FOnCrawlTransitionFinished OnCrawlTransitionFinished;

  // ===== 查询（纯读，任何端都能安全调用）=====

  // 是不是趴着（**复制属性**，所有端都读得到；过渡期间也是 true）
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  bool IsProne() const { return bIsProne; }

  // 是不是正在播一次过渡动作（复制的权威结论，**只有拥有者客户端**收得到）
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  bool IsCrawlTransitionActive() const { return bCrawlTransitionActive; }

  // 本端是否正在播放过渡表现（**不复制**，各端自己维护）。
  //
  // ⚠️ 与复制的 `bCrawlTransitionActive` 的区别：后者是 `COND_OwnerOnly`，
  // 只有拥有者客户端读得到；本函数在**任何端**都反映「我这端正在不在演过渡」。
  // 动画蓝图 / UI 想在所有端都正确工作，请用这个。
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  bool IsCrawlPresentationActive() const { return bCrawlPresentationActive; }

  // 本端正在演的那一次过渡是哪种（各端自己记的，**不复制**）。
  // 用途：动画蓝图选姿态、特效按「趴下 / 起身」分流。
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  ECrawlTransition GetLocalCrawlTransition() const {
    return LocalActiveTransition;
  }

  // 冷却是否已经走完
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  bool IsCrawlReady() const;

  // 剩余冷却时间（秒），0 = 可以发起新的过渡
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  float GetCrawlCooldownRemaining() const;

  // 状态的一行快照，专供日志 / 调试用（每个判定点都会打它）
  UFUNCTION(BlueprintPure, Category = "Crawling", meta = (BlueprintThreadSafe))
  FString GetCrawlDebugString() const;

  // ===== 表现同步（服务器 -> 所有端）=====
  // 引擎默认**不复制蒙太奇播放**（`ACharacter` 只复制 RootMotion 那一段，
  // `FRepRootMotionMontage`），所以想让客户端也看到趴下动画必须显式 Multicast。
  //
  // 参数直接把「演哪一段」带过去，客户端**不读**复制的状态来决定播什么。
  //
  // 本函数负责广播 `OnCrawlTransitionStarted`（表现真的播起来之后），是生命周期广播的唯一入口。
  //
  // ⚠️ 客户端自己调这个 RPC 只会**在本端本地执行一次**，既不发服务器也不广播给别人
  // （引擎行为：`AActor::GetFunctionCallspace` 对 Multicast 在客户端返回 `Local`，
  // 见 `Actor.cpp:5500-5519`）。所以本函数必须做到「在任何端本地执行都安全」：
  // 只播动画 + 维护本端表现标志，不写任何玩法状态。

  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "Crawling|RPC")
  void Multicast_PlayCrawlMontage(ECrawlTransition Transition);

  // 在所有端播放过渡表现（特效 / 音效 / 镜头）。蓝图覆写 `PlayCrawlEffects` 即可。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "Crawling|RPC")
  void Multicast_PlayCrawlEffects(ECrawlTransition Transition);

  // 蓝图覆写的表现入口（纯表现，任何端都可以本地调）。C++ 默认什么都不做。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Crawling")
  void PlayCrawlEffects(ECrawlTransition Transition);

  // ===== 服务器权威函数（BlueprintAuthorityOnly + 内部开发期门禁）=====
  // 客户端直接调用会被引擎静默丢弃（callspace = Absorbed），请改调对应的 `Server_*` RPC。

  // 进入趴下。依次校验：总开关 → 重入（已趴下 / 正在过渡）→ 冷却 → 选定动作 → 播动画。
  // 返回是否**真的播起了动画**（没播起来时姿态与速度仍然生效，见类注释）。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Crawling")
  bool EnterProne();

  // 退出趴下（起身）。校验同上；真正的姿态复位发生在**起立过渡结束时**
  // （过渡期间角色仍然按「趴着」处理：慢速 + 占用角色）。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Crawling")
  bool ExitProne();

  // 一个键来回切：由**服务器**按自己那份 `bIsProne` 决定进还是出
  // （客户端不决定玩法状态，它本地那份可能已经过期）。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crawling")
  void ToggleProne();

  // 强制结束趴下姿态（服务器侧）：复活 / 传送 / 被处决等外部打断时用。
  // 会停掉过渡蒙太奇、把姿态复位成站立并还原本来的速度，同时广播
  // `OnCrawlTransitionFinished`（订阅者据此解锁）。
  // 不在趴下状态、也没有过渡在跑时调用是安全的空操作。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Crawling")
  void ForceEndProne();

  // 清除冷却（服务器侧）：复活 / 传送 / 调试用
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Crawling")
  void ResetCrawlCooldown();

  // ===== 客户端 -> 服务器：趴下请求 =====
  // 输入绑定通常写在蓝图里（Enhanced Input），C++ 侧不重复绑定。
  // `_Validate` 只做廉价检查（这里没有参数，恒为 true）；
  // 真正的重入 / 冷却 / 总开关判定全部在 `EnterProne()` / `ExitProne()` 内部。

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Crawling|RPC")
  void Server_EnterProne();

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Crawling|RPC")
  void Server_ExitProne();

  // 一个键切换的推荐入口：客户端不需要自己判断当前姿态（服务器说了算）
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Crawling|RPC")
  void Server_ToggleProne();

protected:
  // 读取 UCrawlingComponentSettings 的 ini 覆盖并写进本组件。
  // **只在权威端执行**（客户端那份值由 `COND_InitialOnly` 属性复制下来）。
  // 无覆盖时是安全空操作。
  void ApplyGameplaySettingsOverrides();

  // 蒙太奇结束回调。
  //
  // 所有端都会触发（蒙太奇被 Multicast 同步到每个端），但收尾的语义在服务器与客户端
  // 都成立，所以这里不加权威门禁：权威端写玩法状态，客户端只清本端表现标志。
  // **只认 `ActiveCrawlMontage`**：别的蒙太奇结束不该影响趴下状态（见类注释 1）。
  UFUNCTION()
  void OnCrawlMontageEnded(UAnimMontage *Montage, bool bInterrupted);

  // 混合淡出开始时的回调（可选的提前收尾，见 bFinishOnBlendOut）。
  // 与 OnCrawlMontageEnded 分开接，是为了让「提前收尾」与「真正结束」两条路径
  // 各自有明确的语义，而不是在同一个回调里靠标志猜。
  UFUNCTION()
  void OnCrawlMontageBlendingOut(UAnimMontage *Montage, bool bInterrupted);

  // 兜底：动画异常（蒙太奇没触发 OnMontageEnded / AnimInstance 被换掉）时强制收尾，
  // 避免本端永久停在「表现中」、权威端的 `bCrawlTransitionActive` 永久为 true
  // （那会让角色再也发不起下一次趴下 / 起立）。时长按蒙太奇剩余时间估算。
  void HandleCrawlTimeout();

  // 无门禁内核：服务器与客户端都走它（客户端只播动画、不写任何玩法状态）。
  // 权威入口与 Multicast 都转发到这里，避免逻辑写两份。
  // 返回**是否真的播起来了**——调用方据此决定要不要广播 Started 与挂兜底定时器
  // （没播起来就什么都不做，否则会「只落锁、没人解锁」）。
  bool PlayCrawlMontageInternal(ECrawlTransition Transition);

  // 过渡收尾：清定时器 + 复位表现标志与（权威端的）玩法状态。
  //
  // `bLeaveProne` 决定姿态怎么落：
  //   * `true`（起立过渡正常结束 / 强制结束）：`bIsProne = false` 并把基准速度还原；
  //   * `false`（趴下过渡结束 / 超时兜底）：保持趴下姿态与爬行速度。
  // 之所以做成参数而不是「按过渡类型自己推」：强制结束可能发生在任何一段过渡中途，
  // 甚至是「趴着发呆」时（那时根本没有过渡类型可推）。
  //
  // `OnCrawlTransitionFinished` 只在**本端确实广播过 Started**（原本
  // `bCrawlPresentationActive` 为真）时广播，保证 Started / Finished 严格配对。
  // `bStopMontage` = 是否要主动停掉蒙太奇（超时 / 强制结束用；正常结束传 false）。
  void FinishCrawlTransition(const TCHAR *Reason, bool bStopMontage,
                             bool bLeaveProne);

  // 按蒙太奇时长挂一次性兜底定时器（动画没结束时强制收尾）。每个端都会挂。
  void OpenCrawlTimeout();

  // 蒙太奇时长（秒）；取不到时返回 1.0 作为兜底
  float GetMontagePlayLength(const UAnimMontage *Montage) const;

  // 服务器在选择「站立起手」还是「奔跑起手」时用：读当前的**水平**速度
  // （见 `RunTransitionSpeedThreshold` 的说明：不能用 last input vector）
  float GetHorizontalSpeed() const;

  // 起手动作的选择（只在服务器调用一次，结果随 Multicast 发给所有端）
  ECrawlTransition SelectEnterTransition() const;

  // 过渡类型 -> 蒙太奇 / Section。奔跑段没配时回退到站立段（静默回退，
  // 因为「留空」是设计内状态，选择点已经记过一条 INFO）。
  UAnimMontage *ResolveMontageForTransition(ECrawlTransition Transition) const;
  FName ResolveSectionForTransition(ECrawlTransition Transition) const;

  // ===== 速度（趴下变慢 / 起立还原）=====
  // 一律经 `USprintBoostComponent` 的公开 API（见类注释），它同时负责把权威速度
  // 镜像给拥有者客户端；宿主没有该组件时才直接写 `MaxWalkSpeed`。

  // 进入趴下：收掉加速 → 抓「趴下前的基准速度」→ 写入爬行速度
  void ApplyProneSpeed();

  // 起立：把抓下来的基准速度写回去（没抓过就跳过，绝不瞎写一个 0）
  void RestoreBaseSpeed();

  // 写基准速度的唯一出口（含「非法值拒写」的兜底闸门）。返回是否写入成功。
  bool ApplyBaseMaxWalkSpeed(float InSpeed, const TCHAR *Reason);

  USprintBoostComponent *FindSprintBoostComponent() const;

  // 是否已经订阅过动画回调（避免每次过渡都 AddDynamic 一遍）
  bool bMontageCallbacksBound = false;

  // 我们订阅回调时那个 AnimInstance（用来发现「AnimInstance 被换掉」：
  // 换了实例之后旧绑定不会再触发，收尾就只能等兜底定时器）。
  // 弱引用：AnimInstance 的生命周期由 Mesh 掌管，不由我们延长。
  TWeakObjectPtr<UAnimInstance> BoundAnimInstance;

  // 我们正在为「混合淡出提前收尾」而收尾，避免随后到来的 OnMontageEnded 再收一次尾
  bool bFinishingFromBlendOut = false;

  // 本端「过渡表现正在播放」标志。**不复制，每个端各写一份**。
  //
  // 它就是生命周期广播的唯一判据（Started 与 Finished 严格由它配对）：
  //   * 由 `Multicast_PlayCrawlMontage` 在表现**真的播起来之后**置 true 并广播 Started；
  //   * 由 `FinishCrawlTransition` 置 false 并广播 Finished。
  //   表现没播起来时保持 false，于是一个事件都不广播 —— 订阅者不会「只落锁、没人解锁」。
  bool bCrawlPresentationActive = false;

  // 本端正在演的那一次过渡（不复制）。收尾时要把它带进 `OnCrawlTransitionFinished`，
  // 也要用它判断「这次收尾是不是一次起立」。
  ECrawlTransition LocalActiveTransition = ECrawlTransition::None;

  // 趴下前的基准最大速度（用于起立后精确还原）。
  // ⚠️ 必须与 `bProneBaseSpeedCaptured` 一起判：0 是「角色被钉死」的合法速度值，
  // 光看数值分不出「还没抓过」与「基准真的是 0」（见 bug-027）。
  float BaseSpeedBeforeProne = 0.0f;
  bool bProneBaseSpeedCaptured = false;

  // 过渡超时兜底定时器
  FTimerHandle CrawlTimeoutHandle;
};
