// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"

#include "SlideComponent.generated.h"

class UCharacterMovementComponent;

// 滑行生命周期通知。
//
// 与 `ULandRollComponent` 一样是**每端各自本地广播**（服务器与每个客户端各广播一次），
// 驱动它们的是**本端的表现状态** `bSlidePresentationActive`，而不是复制的 `bIsSliding`：
//   * `OnSlideStarted` 在 `Multicast_BeginSlide` 里、本端开始占用角色之后广播；
//   * `OnSlideFinished` 在收尾（`Multicast_EndSlide`：时长到期 / 速度过低 / 离地 / 外部打断）时广播。
// 两者严格配对：本端没广播过 Started 就不会广播 Finished。
//
// ⚠️ 为什么不能拿复制的 `bIsSliding` 当判据：它是 `COND_OwnerOnly`，
// 模拟代理（其他玩家）永远收不到、永远是 false（见 bug-030）。
// 订阅者（`APlayerCharacter` 的移动门控）只认这两个事件：
// 收到 Started 落**软锁**（只记账 + 复位加速，不碰移动模式），收到 Finished 释放。
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlideStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlideFinished);

/**
 * 滑行（低姿态冲刺滑铲）组件 —— **服务器权威**。
 *
 * ## 权威边界一览
 *
 * | 内容 | 权威端 | 客户端 | 通道 |
 * |---|---|---|---|
 * | 滑行判定与状态推进 `StartSlide()` | 服务器 | ✗ 调不动（Absorbed） | `Server_StartSlide` |
 * | 冷却 / 重入 / 总开关 / 起滑速度判定 | 服务器 | ✗ | 在 `StartSlide()` 内部完成 |
 * | 状态 `bIsSliding` / `LastSlideTime` / `SlideCooldown` / `SlideCount` | 服务器写 | 只读（复制下来） | `DOREPLIFETIME_CONDITION(COND_OwnerOnly)` |
 * | **结束时机**（时长 / 速度 / 离地） | 服务器判 | ✗（等 Multicast 通知） | `TickAuthoritySlide()` → `Multicast_EndSlide` |
 * | 蒙太奇播放 | 服务器发起 | 跟随（但不写玩法状态） | `Multicast_BeginSlide(段名)` |
 * | 生命周期广播 `OnSlideStarted/Finished` | 每端各自本地 | 每端各自本地（不复制） | `bSlidePresentationActive` |
 * | 特效 `PlaySlideEffects()` | 服务器发起 | 跟随 | `Multicast_PlaySlideEffects` |
 *
 * ### 滑行的位移为什么**不**改 `MaxWalkSpeed`
 *
 * 位移模型是「**零摩擦 + 每帧把水平速度夹到速度曲线上**」，一个 `MaxWalkSpeed` 都不碰：
 *
 *   * 引擎证据 1：`UCharacterMovementComponent::ApplyVelocityBraking` 在
 *     `Friction == 0 && BrakingDeceleration == 0` 时**直接 return**
 *     （`CharacterMovementComponent.cpp:4310-4316`）。所以把 `GroundFriction` /
 *     `BrakingDecelerationWalking` 置 0，速度就不会被移动组件悄悄吃掉 ——
 *     滑行的减速完全由本组件的曲线决定。
 *   * 引擎证据 2：已有超速的速度**不会**被夹回 `MaxWalkSpeed`。
 *     `CalcVelocity` 在算输入上限时写的是
 *     `NewMaxInputSpeed = IsExceedingMaxSpeed(MaxInputSpeed) ? Velocity.Size() : MaxInputSpeed`
 *     （`CharacterMovementComponent.cpp:3863-3865`），即「本来就超速」时上限取当前速度，
 *     于是 900 的滑行速度不会因为 `MaxWalkSpeed == 600` 被砍掉。
 *   * 为什么要刻意避开 `MaxWalkSpeed`：它已经被 `USprintBoostComponent` 当作自己的独占资源
 *     （`CaptureBaseMaxSpeed` / `ResetMaxSpeed`）。两个组件都去「快照-改写-还原」同一个速度，
 *     只要中途有一方复位，另一方还原时就会把**过期的快照**写回去
 *     （角色永久卡在加速速度上）。这是 bug-027 那类「基准值污染」的翻版，所以这里从设计上绕开。
 *
 * 位移的转向用 `MaxAcceleration` 控制（`bAllowSteering` / `SlideMaxAcceleration`）：
 * `ScaleInputAcceleration` 是 `GetMaxAcceleration() * InputAcceleration`（`:8018`），
 * 置 0 就等于「输入完全不产生加速度」= 纯直线滑行；给一个小值就是「速度不变、方向可微调」——
 * 因为 `CalcVelocity` 在超速时是把 `Velocity + Acceleration * dt` 夹回**原速度大小**（`:3863-3865`），
 * 方向会朝输入方向转、速度不涨。
 *
 * ### 为什么**自主代理也要本地镜像**同一套参数与曲线
 *
 * 摩擦 / 刹车减速度 / 最大加速度**都不是复制属性**（`UCharacterMovementComponent` 在 5.7
 * 只靠 `FRepMovement` 复制位移，本机实测头文件里 `MaxWalkSpeed` / `GroundFriction` 均无
 * `Replicated` 标记）。权威端自己改它们，客户端一无所知 —— 客户端仍按「有摩擦」预测，
 * 速度迅速衰减，位置与服务器越差越远，最后被 `ClientAdjustPosition` 拉回 = **橡皮筋**
 * （bug-029 就是这么表现的）。所以 `Multicast_BeginSlide` 在每个端都执行，其中
 * `ShouldDriveSlideLocally()`（权威端 **或** 本地控制的自主代理）那一份会写下**同一套**移动参数，
 * `TickComponent` 也在同一个端跑**同一条**速度曲线。权威端仍然是唯一真相源：
 * 客户端这些写入只影响**本地预测**，状态与结束时机都由服务器复制 / Multicast 决定。
 * 想验证「关掉镜像会怎样」可以把 `bMirrorSlideOnOwningClient` 关掉对比手感。
 *
 * ### 生命周期广播同样不看 `bIsSliding`（见 bug-030 的教训）
 *
 * `bIsSliding` 是 `COND_OwnerOnly`，模拟代理永远读不到；而移动门控是**每端本地**的，
 * 必须每端都能就地落锁 / 解锁。所以 Started / Finished 的判据是本端那份**不复制**的
 * `bSlidePresentationActive`。想在所有端（动画蓝图 / UI）问「这端是不是正在滑」，
 * 请用 `IsSlidePresentationActive()`，不要用 `bIsSliding`。
 *
 * ### 结束条件是「时长 / 速度 / 离地」，不是动画
 *
 * 本组件**刻意不订阅** `OnMontageEnded`（这点与 `ULandRollComponent` 相反、与
 * `USprintBoostComponent` 一致）：翻滚的收尾本来就该由动画驱动，而滑行是一个由速度曲线
 * 推进的位移动作，动画只是贴在它上面的表现。`SlideMontage` **留空是合法状态**——
 * 位移、状态、冷却全都照常，只记一条 INFO（内容仓库里已有
 * `/Game/Characters/Man/Animations/Montage/SlideMontage`，可以直接填上）。
 *
 * ### 起滑方向怎么定（服务器侧）
 *
 * 依次取：① 当前**水平速度**方向（冲刺中起滑 → 顺着跑动方向滑出去）；
 * ② 没有速度时取**输入加速度**的方向（站着起滑也能朝着玩家推的方向）；③ 再退回角色正前方。
 *
 * ⚠️ 这里**不能**用 `GetLastInputVector()`：它只由客户端 `AddMovementInput` 累加，
 * 服务器上恒为零向量（bug-026）。服务器能拿到的是 move 包里的 `Acceleration`
 * （`CharacterMovementComponent.cpp:10512`），读法是 `GetCurrentAcceleration()`。
 *
 * ### 有意不做的事（要改之前先读这段）
 *
 *   * **不改胶囊体、不调用 `Crouch()`**：本项目目前没有任何蹲伏逻辑（`IA_Crouch` 只在蓝图侧），
 *     而缩小胶囊要处理「头顶有障碍时站不起来」的一整套边界。滑行的低姿态外观由 `SlideMontage`
 *     提供；要让碰撞体也变矮，请在蓝图里自己调 `Crouch()/UnCrouch()`（软锁不会妨碍你）。
 *     代价是：滑行期间碰撞高度仍是站立高度，低矮通道里会撞头。
 *   * **不用根运动**：`SlideMontage` 未启用根运动（资源名字表里没有 `bEnableRootMotion`），
 *     位移一律由本组件的速度曲线驱动，这样才可预测、可被撞墙/上坡自然打断。
 *
 * ⚠️ `UActorComponent` 上没有 `HasAuthority()`（那是 `AActor` 的方法，直接写会 C3861），
 * 组件里一律用 `Utils/WildforgeAuthority.h` 的 `IsAuthoritativeForActorComponent(this)`。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API USlideComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  USlideComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  virtual void BeginPlay() override;

  // 销毁时收尾本端的移动参数改写：滑行中途组件被移除（或角色销毁）而
  // `GroundFriction` / `MaxAcceleration` 留在滑行值上，角色会永久「踩着冰」。
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  // ===== 可配置参数（类默认值，随 Actor 生成同步，不参与运行时复制）=====

  // 能不能滑行的总开关（眩晕 / 死亡 / 缴械等状态把它关掉）。
  // 关闭时 `StartSlide()` 直接拒绝；**已经滑到一半**时把它关掉也会在下一帧收尾。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide")
  bool bCanSlide = true;

  // 滑行时长（秒）：速度曲线在这段时间内从起始速度衰减到 `SlideEndSpeed`。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide",
            meta = (ClampMin = "0.01", UIMin = "0.01", ForceUnits = "s"))
  float SlideDuration = 1.0f;

  // 速度曲线的**左端下限**（cm/s）。起手时取 `max(当前水平速度, SlideStartSpeed)`，
  // 所以它只保证「至少这么快」——冲刺中起滑不会被减速（动量优先）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
  float SlideStartSpeed = 900.0f;

  // 速度曲线的**右端**（cm/s）：`SlideDuration` 走完时的目标速度。
  // 它高于 `SlideMinSpeedToContinue` 时，滑行才会走满时长后自然结束。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
  float SlideEndSpeed = 300.0f;

  // 速度被夹到曲线上的**最大下降速率**（cm/s²）。用来避免「带着外部高速（击飞 / 爆炸）
  // 起滑」时被曲线一刀切成低速：超速部分按这个速率削，削到曲线值为止。
  // 0 = 不限制（立刻夹到曲线值）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Advanced",
            meta = (ClampMin = "0", UIMin = "0"))
  float SlideMaxSpeedDecelRate = 1500.0f;

  // 低于这个水平速度就提前结束滑行（撞墙 / 上坡 / 被夹住）。0 = 不检查。
  //
  // ⚠️ 它要**小于** `SlideEndSpeed`，否则滑行会在中途就被这条规则截断。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
  float SlideMinSpeedToContinue = 150.0f;

  // 速度检查的宽限时间（秒）：起滑后的这一小段时间内不看最低速度，
  // 免得「站着起滑 + 首帧速度还没写进移动组件」被立刻判成速度过低。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Advanced",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float SlideMinSpeedGraceTime = 0.15f;

  // 起滑速度要求（cm/s）：当前水平速度低于它就拒绝本次滑行。
  // 0 = 站着也能滑（配合 `SlideStartSpeed` 就是一个「滑铲冲刺」）；
  // 设成 300 之类就变成「必须跑起来才能滑」。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
  float MinSpeedToStartSlide = 0.0f;

  // 滑行期间是否允许有限转向（靠 `SlideMaxAcceleration` 给一点输入加速度）。
  // 关掉 = 纯直线滑行（把 `MaxAcceleration` 置 0，输入完全不产生加速度）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Steering")
  bool bAllowSteering = true;

  // 滑行期间的 `MaxAcceleration`：转向的**唯一**强度来源（不是速度上限）。
  // 越大越灵活，默认 300 大约是一秒能偏转几十度。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Steering",
            meta = (ClampMin = "0", UIMin = "0",
                    EditCondition = "bAllowSteering"))
  float SlideMaxAcceleration = 300.0f;

  // 滑行期间的地面摩擦（默认 0 = 完全不减速，减速只由速度曲线负责）。
  // ⚠️ 它与 `SlideBrakingDeceleration` **同时为 0** 时引擎才会跳过刹车计算
  // （`ApplyVelocityBraking` 的早退，见类注释），只置 0 一个仍然会被另一个减速。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Advanced",
            meta = (ClampMin = "0", UIMin = "0"))
  float SlideGroundFriction = 0.0f;

  // 滑行期间的步行刹车减速度（默认 0，理由同上）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Advanced",
            meta = (ClampMin = "0", UIMin = "0"))
  float SlideBrakingDeceleration = 0.0f;

  // 离地（跳跃 / 掉下平台 / 被顶起）就结束滑行。位移模型（贴地滑）只在地面成立。
  // 想在矮台阶上「飞过去」可以关掉：曲线在空中的夹紧照常生效，只是没有摩擦可言。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Advanced")
  bool bEndSlideWhenAirborne = true;

  // 冷却（秒）：两次滑行开始时间的最小间隔。0 = 不限制。
  //
  // 它既是配置（EditAnywhere）**也**在复制列表里，这是有意的（同 `LandRollCooldown`）：
  // 客户端算「冷却还剩几秒」的 UI 要和服务器用同一份数值。
  // 玩法判定始终在服务器，客户端改它只影响自己的本地副本。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Slide",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float SlideCooldown = 0.5f;

  // 在本地控制的自主代理上也跑同一套移动参数与速度曲线（本地预测镜像）。
  // 关掉它 = 只有服务器改参数，客户端按默认摩擦预测 → 联机时位置会被反复纠正
  // （就是 bug-029 那种橡皮筋）。默认开；关掉只建议用于排查对比。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Advanced")
  bool bMirrorSlideOnOwningClient = true;

  // ===== 可选表现：滑行蒙太奇 =====
  //
  // **留空是完全合法的**（记 INFO，不是 WARNING）：滑行的位移与时长由本组件推进，
  // 动画只是表现。内容仓库里的资源是
  // /Game/Characters/Man/Animations/Montage/SlideMontage（对应 anim_Slide_L）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Anim")
  TObjectPtr<UAnimMontage> SlideMontage;

  // 蒙太奇播放速率倍率（1 = 原速）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Anim",
            meta = (ClampMin = "0.01", UIMin = "0.01"))
  float MontagePlayRate = 1.0f;

  // 只在指定 Section 播放（`NAME_None` = 从第 0 段 / 蒙太奇开头播）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Anim")
  FName SlideSectionName = NAME_None;

  // 收尾时停掉蒙太奇的淡出时长（秒）：滑行结束时动画要平滑接回移动状态。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Slide|Anim",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float MontageStopBlendOutTime = 0.15f;

  // ===== 状态（服务器写、拥有者客户端只读）=====

  // 是否正在滑行。服务器权威；拥有者客户端可以拿它做 UI / 输入门控，
  // 但**不要**用它做「本端在不在演滑行」的判断（那是 `COND_OwnerOnly`，模拟代理永远读不到）。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Slide")
  bool bIsSliding = false;

  // 最近一次滑行开始的世界时间（秒，服务器时钟）。只在服务器上读写（冷却判定依据），
  // 复制给客户端只是为了调试与 UI 显示。
  //
  // ⚠️ 初值是 -1000 而不是 0：用 0 会让「刚 BeginPlay、游戏时间还没到冷却时长」
  // 的那几秒里滑行被冷却误挡（`Now - 0 < Cooldown`）。
  // ⚠️ 它是**服务器时钟**：客户端千万不要拿它和自己的 `World->GetTimeSeconds()` 相减算进度
  // （两端世界时间起点不同）。本端要算进度用 `GetSlideAlpha()`，它读的是各端自己记下的起滑时间。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Slide")
  float LastSlideTime = -1000.0f;

  // 成功滑行的累计次数（调试 / UI 用）
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Slide")
  int32 SlideCount = 0;

  // 当前这一段实际在播的蒙太奇。
  //
  // 与 `ULandRollComponent::ActiveLandRollMontage` 一样**在每个端各写一份**
  // （`PlaySlideMontageInternal` 里写），同时参与复制只是为了「还没轮到执行 Multicast 的端」
  // 也能看到状态用于 UI / 调试。收尾时用它来 `Montage_Stop`，所以不要用配置项代替它。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Slide")
  TObjectPtr<UAnimMontage> ActiveSlideMontage;

  // ===== 通知（每端各自本地广播）=====

  UPROPERTY(BlueprintAssignable, Category = "Slide")
  FOnSlideStarted OnSlideStarted;

  UPROPERTY(BlueprintAssignable, Category = "Slide")
  FOnSlideFinished OnSlideFinished;

  // ===== 查询（纯读，任何端都能安全调用）=====

  // 滑行状态的一行快照，专供日志 / 调试用（每个判定点都会打它）
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  FString GetSlideDebugString() const;

  // 冷却是否已经走完（只看冷却，不看总开关与重入）
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  bool IsSlideReady() const;

  // 现在能不能起一次滑行：总开关 + 不在滑行中 + 冷却已走完。
  // ⚠️ 这是**查询**，用于蓝图 UI / 输入节流；真正的判定在 `StartSlide()` 内部（服务器）。
  // 客户端读到的 `bIsSliding` 有半个 RTT 的滞后，所以它只适合做提示，不适合做门禁。
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  bool IsSlideAvailable() const;

  // 剩余冷却时间（秒），0 = 可以滑
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  float GetSlideCooldownRemaining() const;

  // 本端是否正在演滑行（**不复制**，各端自己维护）。
  //
  // ⚠️ 与复制的 `bIsSliding` 的区别：那个是 `COND_OwnerOnly`，只有拥有者客户端读得到；
  // 本函数在**任何端**都反映「我这端正在不在滑」。动画蓝图 / UI 想在所有端都正确工作，
  // 请用这个（而不是 `bIsSliding`）。
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  bool IsSlidePresentationActive() const { return bSlidePresentationActive; }

  // 本次滑行在速度曲线上的进度 0..1（0 = 刚起手，1 = 曲线走完）；不在滑行时为 0。
  // 读的是**本端**记下的起滑时间，所以在每个端都成立（不受两端时钟差影响）。
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  float GetSlideAlpha() const;

  // 速度曲线在当前进度下的目标速度（cm/s）：`Lerp(起始速度, SlideEndSpeed, Alpha)`。
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  float GetDesiredSlideSpeed() const;

  // 当前水平速度（cm/s，忽略 Z）。调试 / UI 用；滑行中它应当跟着目标速度一起衰减。
  UFUNCTION(BlueprintPure, Category = "Slide", meta = (BlueprintThreadSafe))
  float GetCurrentHorizontalSpeed() const;

  // ===== 表现同步（服务器 -> 所有端）=====
  // 引擎默认**不复制蒙太奇播放**（`ACharacter` 只复制 RootMotion 那一段的
  // `FRepRootMotionMontage`），所以想让别人看到你滑出去必须显式 Multicast。
  //
  // 参数直接把「播哪一段」带过去，客户端**不读**复制的状态来决定播什么：
  // RPC 与属性复制谁先到不保证，靠读状态会让客户端偶尔播错。
  //
  // ⚠️ 客户端自己调这个 RPC 只会**在本端本地执行一次**，既不发服务器也不广播给别人
  // （引擎行为：`AActor::GetFunctionCallspace` 对 Multicast 在客户端返回 `Local`，
  // 见 `Actor.cpp:5500-5519`）。所以它必须做到「在任何端本地执行都安全」：
  // 只播动画 + 维护本端表现标志与**本地预测**用的移动参数，不写任何玩法状态。
  //
  // 本函数是 `OnSlideStarted` 的唯一广播入口（每个端恰好广播一次）。
  //
  // ⚠️ 刻意**不开放给蓝图**：它不只播动画，还会写本端移动参数并广播 Started，
  // 蓝图若单独调它就会「落了锁却没有对应的结束路径」（Started/Finished 不成对 = 角色被占住）。
  // 蓝图要起手就走 `Server_StartSlide`（服务器上就地同步执行，单机与联机同一条路径）。
  UFUNCTION(NetMulticast, Reliable, Category = "Slide|RPC")
  void Multicast_BeginSlide(FName InSectionName);

  // 滑行收尾（服务器判定的所有结束路径都走它）：停蒙太奇 + 还原本端移动参数
  // + 广播 `OnSlideFinished`（仅当本端广播过 Started）。
  //
  // ⚠️ 同样**不能**在实现里加「非权威端就忽略」的门禁：客户端收到它就是正常接收，
  // 加门禁等于客户端永远收不了尾（bug-030 踩过）。
  //
  // ⚠️ 也不开放给蓝图：单独调它会把表现收掉而 `bIsSliding` 还挂着（客户端更不该碰玩法状态）。
  // 蓝图要提前结束就走 `Server_StopSlide` / 服务器侧 `StopSlide`。
  UFUNCTION(NetMulticast, Reliable, Category = "Slide|RPC")
  void Multicast_EndSlide();

  // 在所有端播放滑行表现（尘土 / 音效 / 镜头）。蓝图覆写 `PlaySlideEffects` 即可。
  // 与 `ULandRollComponent` 一样，C++ 不主动调用它，交给蓝图在合适的时机（例如起手那帧）调。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Slide|RPC")
  void Multicast_PlaySlideEffects();

  // 蓝图覆写的表现入口（纯表现，任何端都可以本地调）。C++ 默认什么都不做。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Slide")
  void PlaySlideEffects();

  // ===== 服务器权威函数（BlueprintAuthorityOnly + 内部开发期门禁）=====
  // 客户端直接调用会被引擎静默丢弃（callspace = Absorbed），请改调 `Server_StartSlide`。

  // 发起一次滑行。
  // 依次校验：总开关 → World → 移动组件 → 重入（已在滑行）→ 冷却 → 起滑速度要求 → 是否在地面。
  // 返回是否**真的开始了**本次滑行（被拒时记一条 WFLOG_WARNING / WFLOG_INFO 说明原因）。
  //
  // 参数一律取自组件配置（没有函数参数）：这是刻意的——本项目踩过
  // 「`_Validate` 的接受范围与实现不一致」的坑（bug-028），无参数就从根上避免这类分歧。
  // 需要不同手感的滑行就做多个组件实例 / 在蓝图里改属性。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Slide")
  bool StartSlide();

  // 立刻结束当前滑行（服务器侧）：松开按键 / 被打击 / 状态机切换时由服务器蓝图调。
  // 会走完整收尾路径（停蒙太奇、还原移动参数、广播 `OnSlideFinished`）。
  // 未在滑行时调用是安全的空操作。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Slide")
  void StopSlide();

  // 清除冷却（服务器侧）：复活 / 传送 / 调试用
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Slide")
  void ResetSlideCooldown();

  // ===== 客户端 -> 服务器：滑行请求 =====
  // 输入绑定通常写在蓝图里（Enhanced Input：例如「冲刺 + 下蹲」触发起手、松开下蹲收手）。
  // C++ 侧不重复绑定输入，避免与蓝图绑定叠加。
  // `_Validate` 只做廉价检查（这里没有参数，恒为 true）；
  // 真正的重入 / 冷却 / 总开关判定全部在 `StartSlide()` 内部。

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Slide|RPC")
  void Server_StartSlide();

  // 提前结束滑行（未在滑行时调用无害，可放心重复发）
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Slide|RPC")
  void Server_StopSlide();

protected:
  // Called every frame
  //
  // 本组件**始终开着 Tick**，靠开头的 `bSlidePresentationActive` 早退（不在滑行时就是一次
  // 分支 + 一次 `HasAuthority()`）。为什么不学 `ULandRollComponent` 干脆关掉 Tick、
  // 也不在起手 / 收尾时 `SetComponentTickEnabled` 开关：滑行的位移必须**逐帧**推进，
  // 而收尾恰恰发生在自己的 Tick 里，那一刻 `SetComponentTickEnabled(false)` 会去
  // `FTickTaskLevel::RemoveTickFunction` 改 tick 列表（`TickTaskManager.cpp:2434-2456`），
  // 在 tick 执行中改这张表属于自找麻烦。一个早退分支的代价远比这条风险小。
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  // 权威端每帧推进：判结束条件（总开关被关 / 离地 / 时长到期 / 速度过低）
  void TickAuthoritySlide();

  // 把水平速度夹到速度曲线上（只往下夹，不往上抬：动量多的部分留着，
  // 想「一路加速到固定速度」才需要往上写）。
  // 在权威端是玩法模拟，在本地控制的自主代理上是预测镜像。
  void ApplySlideSpeedCurve(float DeltaTime);

  // 整段滑行收尾（**只在权威端调用**）：清兜底定时器 + 复位玩法状态 +
  // `Multicast_EndSlide()`（服务器上这次调用就是本地执行，所以权威端不需要再单独收一次尾）。
  void FinishSlide(const TCHAR *Reason);

  // 兜底：Tick 因为任何原因没在跑（外部关了组件 Tick / 帧更新异常）时强制收尾，
  // 避免 `bIsSliding` 永久为 true 把重入与冷却判定全卡死。
  void HandleSlideTimeout();
  // 按 `SlideDuration` + 余量挂一次性兜底定时器
  void OpenSlideTimeout();

  // 无门禁内核：任何端都能调（不写玩法状态）。返回是否**真的播起来了**；
  // 没播起来不代表滑行失败（蒙太奇为空是合法状态），只影响动画表现。
  bool PlaySlideMontageInternal(FName InSectionName);
  // 停掉本端正在播的滑行蒙太奇（带淡出）
  void StopActiveSlideMontage();

  // 本端写入 / 还原「滑行专用」的移动参数（`GroundFriction` / `BrakingDecelerationWalking` /
  // `BrakingFriction` / `MaxAcceleration`）。
  // 幂等：写之前先看 `RuntimeState.bMovementOverridesApplied` / 快照有效性，**不靠数值判断**
  // （bug-027 的教训：0 既是合法值也是「还没捕获」的初值，光看数字分不出来）。
  void ApplySlideMovementOverrides();
  void RestoreSlideMovementOverrides(const TCHAR *Reason);

  // 起手冲量：把水平速度设成 `方向 * max(当前水平速度, SlideStartSpeed)`（Z 分量不动）。
  // 返回本次实际使用的曲线起始速度。
  float ApplySlideImpulse();

  // 起滑方向：① 水平速度方向 → ② 输入加速度方向（服务器上唯一有效的输入方向，见 bug-026）
  // → ③ 角色正前方
  FVector ResolveSlideDirection() const;

  // 本端要不要驱动滑行的位移：权威端总是要；本地控制的自主代理在
  // `bMirrorSlideOnOwningClient` 打开时也要（本地预测，见类注释）。
  bool ShouldDriveSlideLocally() const;

  // 宿主是否「这台机器上由玩家直接操作」（自主代理 / 单机）
  bool IsLocallyControlledOwner() const;

  // 我们正在为「本端表现」而收尾，避免 Started / Finished 之外的状态被重复处理
  bool bSlidePresentationActive = false;

private:
  // ===== 滑行的内部状态 =====
  // 全部只在「本端确实驱动了位移」的端有意义（权威端 + 本地控制的自主代理），不复制。
  struct FSlideRuntimeState {
    // 本端本次滑行的起始时刻（**本端世界时间**，不是复制的服务器时间）：
    // 速度曲线与冷却剩余都拿它算差值，所以两端各用各的时钟就不会串味。
    float LocalSlideStartTime = -1000.0f;

    // 本次曲线实际使用的起始速度（`max(起手时的水平速度, SlideStartSpeed)`）。
    // 起手那一刻算一次并记下来，之后曲线只用它——中途再改会被别人碰过的速度带偏。
    float SlideStartSpeedEffective = 0.0f;

    // 被我们改写的移动参数原值。
    //
    // ⚠️ `bValid` 是必须的：`GroundFriction` / `MaxAcceleration` 的 0 都是**合法值**
    // （0 摩擦、0 加速度），光看数值无法区分「原值就是 0」和「还没快照过」。
    // 任何在快照之前写回的地方都会把角色永久钉在原地 / 变成溜冰——bug-027 就是这么发生的。
    struct FMovementSnapshot {
      float GroundFriction = 0.0f;
      float BrakingDecelerationWalking = 0.0f;
      float BrakingFriction = 0.0f;
      float MaxAcceleration = 0.0f;
      bool bValid = false;
    } MovementSnapshot;

    // 本端当前是否写入了滑行用的移动参数（决定能不能 / 要不要还原）
    bool bMovementOverridesApplied = false;
  };

  FSlideRuntimeState RuntimeState;

  // 滑行超时兜底定时器（只在权威端挂）
  FTimerHandle SlideTimeoutHandle;

  // 取宿主身上的移动组件（没有就返回 nullptr，调用方各自记日志）
  UCharacterMovementComponent *GetOwnerMovementComponent() const;
};
