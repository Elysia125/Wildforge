// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"

#include "SprintBoostComponent.generated.h"

// 加速进度的本地广播。
//
// 与「复制属性」的分工：本委托在**服务器与每个客户端各自本地广播**，订阅者
// （UI 冲刺条 / 镜头 FOV / 后处理）能就地立即响应，不必等状态复制往返一个 RTT。
// 服务器侧由 TickSprintBoost 每帧触发；客户端侧不会自动触发（除非显式开
// `bDriveOwnerLocally`，见下），需要每帧数值的客户端请自己按
// `GetBoostAlpha()` 插值，或直接开本地驱动。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSprintBoostUpdated,
                                            float, BoostAlpha);

// 生命周期通知：加速开始 / 结束（含到顶后 Reset、攻击打断等所有结束路径）。
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSprintBoostStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSprintBoostStopped);

/**
 * 加速（长按冲刺）组件 —— **服务器权威**。
 *
 * ## 权威边界一览
 *
 * | 内容 | 权威端 | 客户端 | 通道 |
 * |---|---|---|---|
 * | 加速曲线推进 `StartSpeedBoost()` | 服务器 | ✗ 调不动（Absorbed） | `Server_StartSpeedBoost` |
 * | 复位 `ResetMaxSpeed()` | 服务器 | ✗ | `Server_StopSpeedBoost` |
 * | 基准速度改写 `SetBaseMaxSpeed()` | 服务器 | ✗ | 服务器蓝图 / 装备系统 |
 * | **`MaxWalkSpeed` 本身** | 服务器写 | 跟随 | **`UCharacterMovementComponent` 自带复制，无需我们再复制一份** |
 * | `bBoostActive` / `BoostAlpha` | 服务器写 | 只读（复制下来） | `DOREPLIFETIME_CONDITION(COND_OwnerOnly)` |
 * | 蒙太奇播放（可选） / 表现特效 | 服务器发起 | 跟随 | `Multicast_PlaySprintMontage` / `Multicast_PlaySprintEffects` |
 *
 * ## 设计要点（两条，都是从旧实现踩过来的）
 *
 * 1. **真正的权威状态只有「CharacterMovement 的 MaxWalkSpeed」一个**：它由
 *    `UCharacterMovementComponent` 自带复制（`OnRep_MaxWalkSpeed`）。所以本能力全部在
 *    权威端跑——客户端自己算一套速度只会和服务器打架（客户端改自己的 `MaxWalkSpeed`
 *    既留不住、又会让服务器回滚）。
 *
 * 2. **`BoostInterval = 0` 必须走 `SetTimerForNextTick` 链，不能交给 `SetTimer`**：
 *    `FTimerManager::SetTimer` 的 `InRate <= 0` 语义是「清掉该句柄上的定时器」
 *    （`TimerManager.h:157`、`TimerManager.cpp:617-659`），传 0 的结果是定时器根本
 *    不存在、一次都不触发（本项目实测踩过，见 bug-017）。详见 `TickSprintBoost()`。
 *
 * ⚠️ `UActorComponent` 上没有 `HasAuthority()`（那是 `AActor` 的方法，直接写会 C3861），
 * 组件里一律用 `Utils/WildforgeAuthority.h` 的 `IsAuthoritativeForActorComponent(this)`。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API USprintBoostComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  USprintBoostComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  // 销毁时清掉定时器：next-tick 链的回调挂着 `this`，组件没了还挂着会打到半销毁对象上。
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  // ===== 可配置参数（类默认值，随 Actor 生成同步，不参与运行时复制）=====

  // 加速的总开关（眩晕 / 缴械 / 死亡 / 重伤等把角色锁住的状态把它关掉）。
  // 关闭时 `StartSpeedBoost()` 直接拒绝，并让已经在跑的加速立刻收尾——
  // 只挡新请求不够，否则「加速中被眩晕」会一直冲下去。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost")
  bool bCanSprint = true;

  // 从起始速度线性升到目标速度需要的总时间（秒）。
  // 只作为 `StartSpeedBoost()` 未显式传参时的默认值（函数参数优先）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost",
            meta = (ClampMin = "0.01", UIMin = "0.01", ForceUnits = "s"))
  float DefaultBoostDuration = 1.5f;

  // 定时器刷新间隔（秒）。0 = 每帧刷新一次（`SetTimerForNextTick` 链，与帧率同步、
  // 最平滑）；正数 = 固定间隔刷新，帧率波动时曲线更可预测。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float DefaultBoostInterval = 0.0f;

  // 提交给移动组件的 `MaxWalkSpeed` 下限（cm/s）：目标速度若低于它会被收敛。
  // 默认 1 只是防「配置成 0 之后角色彻底动不了」，正常不参与运算。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm/s"))
  float MinTargetSpeed = 1.0f;

  // ===== 方向门控：只有向前移动时才允许加速 =====
  //
  // 开启后，加速**进行中**每帧检查移动输入方向与角色正前方的夹角，超出
  // `MaxForwardSprintAngle` 就立刻结束本次加速（复位到基准速度）。所以：
  //   * 按 W + Shift 才加速；侧移（A/D）、倒退（S）都不加速；
  //   * 加速到顶以后再转向侧后方，同样会被收掉（不是在起手时判一次就算数）；
  //   * 非步行状态（`MOVE_Falling` 等，含跳跃与下落）一律不判定——跳跃中不该把
  //     冲刺打断（UE 默认 `AirControl=0.05`，空中按方向键几乎不产生 Acceleration），
  //     落地回到 `MOVE_Walking` 后立即恢复判定。
  //
  // 为什么方向判定写在 C++ 而不是蓝图：
  //   1. **服务器上 `GetLastInputVector()` 恒为零向量**。它读的是
  //      `APawn::LastControlInputVector`（Pawn.cpp:819-822），只由客户端
  //      `AddMovementInput` 累加；服务器收到的是 move 包里的 `Acceleration`
  //      （`MoveAutonomous` 内 `Acceleration = ConstrainInputAcceleration(NewAccel)`，
  //      CharacterMovementComponent.cpp:10512）。所以在服务器/权威侧做方向判定
  //      必须用 `GetCurrentAcceleration()`，蓝图里那个 `GetLastInputVector` 只在
  //      客户端有值——在服务器上会静默失效（恒为 0）。
  //   2. `MaxWalkSpeed` 是服务器权威的。只在客户端蓝图判方向的话，转向后服务器
  //      还在给加速速度，要等一个 RTT 才收——表现为「方向已经转过去了、加速还挂着」。
  //   3. 调用方不再各自实现一遍：任何走 `Server_StartSpeedBoost` 的入口自动获得同一套规则。
  //
  // 客户端**也**在本地跑同一套判定（`bDriveOwnerLocally` 开启时），保证本地预测与
  // 服务器结论一致；权威结论始终在服务器。
  //
  // 默认 true：默认行为就是「向前才加速」，关掉即恢复旧行为（按住 Shift 就加速）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost|Direction")
  bool bSprintOnlyForward = true;

  // 「算作向前」的最大夹角（度）。0 = 只能严格正前方（过于苛刻，不建议）；
  // 180 = 等于关掉方向判定。给一点余量是必须的——输入是模拟量（手柄摇杆），
  // 而且在拐角/绕行时手指不会精确压在前方，60 度是个手感还行的起点。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost|Direction",
            meta = (ClampMin = "0", ClampMax = "180", UIMin = "0", UIMax = "180",
                    EditCondition = "bSprintOnlyForward"))
  float MaxForwardSprintAngle = 60.0f;

  // 方向门控忽略输入向量的长度下限：移动输入长度低于它时视为「本帧没有方向输入」，
  // 不判定（保持原有加速状态）。用 `KINDA_SMALL_NUMBER` 而不是更大的值，
  // 是为了不把「手柄轻推摇杆」误判成无输入。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost|Direction",
            meta = (ClampMin = "0", EditCondition = "bSprintOnlyForward"))
  float SprintDirectionInputThreshold = KINDA_SMALL_NUMBER;

  // ===== 可选表现：加速蒙太奇 =====
  //
  // **留空是完全合法的**：内容仓库里当前并没有冲刺用的蒙太奇资源，所以默认就是空的，
  // 此时 `Multicast_PlaySprintMontage` 会跳过播放并记一条 INFO（不是 WARNING，
  // 空配置是设计内的状态而不是错误）。配上一个「冲刺起手」蒙太奇即可自动生效。
  //
  // 注意：这里刻意**不**像 `ULandRollComponent` 那样把蒙太奇播放挂在
  // `OnMontageEnded` 上——加速的结束条件是速度曲线 / 玩家松键，不是动画播完。
  // 动画只作为表现叠加，播完了也不该影响加速状态（滚动动画会打断移动蒙太奇，
  // 但加速是速度层的东西，两者互不依赖）。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost|Anim")
  TObjectPtr<UAnimMontage> SprintMontage;

  // 蒙太奇播放速率倍率（1 = 原速）。只影响这个可选表现，不影响速度曲线。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SprintBoost|Anim",
            meta = (ClampMin = "0.01", UIMin = "0.01"))
  float SprintMontagePlayRate = 1.0f;

  // ===== 状态（服务器写、拥有者客户端只读）=====

  // 是否处于「加速中」——含已经到顶、定时器已停但速度仍保持的状态，
  // 直到 `ResetMaxSpeed()` 才变 false。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "SprintBoost")
  bool bBoostActive = false;

  // 加速进度 0..1（0 = 刚起步，1 = 已到顶）。复制给拥有者客户端做 UI，
  // 免得客户端只能靠 `MaxWalkSpeed` 反推进度。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "SprintBoost")
  float BoostAlpha = 0.0f;

  // 上一次加速目标速度（cm/s），供 UI / 调试显示；不参与判定。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "SprintBoost")
  float LastTargetSpeed = 0.0f;

  // ===== 通知（每端各自本地广播）=====

  // 每次刷新速度（每帧 / 固定间隔）时触发，参数是当前进度 0..1。
  // 客户端可在这里做 FOV / 冲刺特效；**不要**在这里改速度相关的权威状态。
  UPROPERTY(BlueprintAssignable, Category = "SprintBoost")
  FOnSprintBoostUpdated OnSprintBoostUpdated;

  // 加速开始（`StartSpeedBoost()` 真的接受了请求）
  UPROPERTY(BlueprintAssignable, Category = "SprintBoost")
  FOnSprintBoostStarted OnSprintBoostStarted;

  // 加速结束（复位 / 到顶后复位 / 被 `bCanSprint=false` 中断 /
  // 开启 `bSprintOnlyForward` 后因转向侧后方被方向门控收掉）
  UPROPERTY(BlueprintAssignable, Category = "SprintBoost")
  FOnSprintBoostStopped OnSprintBoostStopped;

  // ===== 表现同步（服务器 -> 所有端）=====
  // 引擎默认**不复制蒙太奇播放**（`ACharacter` 只复制 RootMotion 那一段，
  // `FRepRootMotionMontage`），所以想让客户端也看到冲刺动画必须显式 Multicast。

  // 在所有端播放可选的冲刺蒙太奇。`InSectionName` 为 `NAME_None` 时从头播。
  // 服务器调用才会转成 Multicast；客户端调用只会本地播放（纯表现，无害）。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "SprintBoost|RPC")
  void Multicast_PlaySprintMontage(FName InSectionName);

  // 在所有端播放冲刺表现（特效 / 音效 / 镜头 FOV 变化）。
  // 蓝图覆写 `PlaySprintEffects` 即可实现具体效果。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "SprintBoost|RPC")
  void Multicast_PlaySprintEffects();

  // 蓝图覆写的表现入口（纯表现，任何端都可以本地调）。
  // C++ 默认什么都不做。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "SprintBoost")
  void PlaySprintEffects();

  // ===== 查询（纯读，任何端都能安全调用）=====

  // 是否正处于加速中（与复制的 `bBoostActive` 同源，供 C++ 方即读取用）
  UFUNCTION(BlueprintPure, Category = "SprintBoost",
            meta = (BlueprintThreadSafe))
  bool IsSpeedBoostActive() const { return BoostState.bBoostActive; }

  // 当前加速进度 0..1；未加速时为 0
  UFUNCTION(BlueprintPure, Category = "SprintBoost",
            meta = (BlueprintThreadSafe))
  float GetSpeedBoostAlpha() const;

  // 当前「未加速时」的基准 MaxWalkSpeed，也就是 `ResetMaxSpeed()` 会回到的值
  UFUNCTION(BlueprintPure, Category = "SprintBoost",
            meta = (BlueprintThreadSafe))
  float GetBaseMaxWalkSpeed() const { return BoostState.BaseMaxWalkSpeed; }

  // 当前（未加速时的）基准蹲伏速度
  UFUNCTION(BlueprintPure, Category = "SprintBoost",
            meta = (BlueprintThreadSafe))
  float GetBaseMaxWalkSpeedCrouched() const {
    return BoostState.BaseMaxWalkSpeedCrouched;
  }

  // 加速状态的一行快照，专供日志 / 调试用（每个判定点都会打它）。
  // 含方向门控的当前判定结果（夹角、是否算向前）。
  UFUNCTION(BlueprintPure, Category = "SprintBoost",
            meta = (BlueprintThreadSafe))
  FString GetSprintBoostDebugString() const;

  // 当前移动输入方向与角色正前方的夹角（度）；没有移动输入时为 0。
  // ⚠️ 无输入与「正前方」都返回 0——要区分请同时看 `IsSprintInputDirectionForward()`。
  UFUNCTION(BlueprintPure, Category = "SprintBoost|Direction",
            meta = (BlueprintThreadSafe))
  float GetSprintInputForwardAngle() const {
    return EvaluateSprintDirection().InputAngleDegrees;
  }

  // 当前移动输入是否算「向前」（按 `MaxForwardSprintAngle` 判定，且已考虑
  // 空中不判定、无输入放行）。UI 想显示「冲刺条件满足」可以直接读它。
  UFUNCTION(BlueprintPure, Category = "SprintBoost|Direction",
            meta = (BlueprintThreadSafe))
  bool IsSprintInputDirectionForward() const {
    return EvaluateSprintDirection().bInputDirectionIsForward;
  }

  // ===== 服务器权威函数（BlueprintAuthorityOnly + 内部开发期门禁）=====
  // 客户端直接调会被引擎静默丢弃（callspace = Absorbed），请改调下方 `Server_*`。

  // 开始一次加速：在 `BoostDuration` 秒内把 `MaxWalkSpeed` 从当前值**线性插值**
  // 提升到 `TargetMaxSpeed`，到达后速度保持，直到 `ResetMaxSpeed()`（或再次调用
  // 本函数改为新的目标）。
  //
  // 参数：
  //   HoldThreshold  长按阈值（秒）。纯调用方节奏参数，**不参与速度计算**——调用方
  //                  需要先自己等满这个时长再调用（或用它做节流），保证「点按」不误触发。
  //   TargetMaxSpeed 加速后的最大速度（cm/s），必须 > 基准速度
  //   BoostDuration  从起始速度到达目标速度需要的时间（秒），< 0 时取
  //                  `DefaultBoostDuration`；= 0 也是合法的（瞬时到顶）
  //   BoostInterval  定时器刷新间隔（秒），< 0 时取 `DefaultBoostInterval`；
  //                  0 = 每帧刷新（`SetTimerForNextTick` 链）；正数 = 固定间隔。
  //                  ⚠️ 不能把 0 直接交给 `FTimerManager::SetTimer`，见类注释。
  //
  // 返回是否真的开始了本次加速（参数非法 / 已在加速中 / 没有移动组件会返回 false
  // 并记一条 WFLOG_WARNING）。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "SprintBoost")
  bool StartSpeedBoost(float HoldThreshold, float TargetMaxSpeed,
                       float BoostDuration = -1.0f,
                       float BoostInterval = -1.0f);

  // 复位最大速度：清掉加速定时器，并把 `MaxWalkSpeed` / `MaxWalkSpeedCrouched`
  // **精确还原**到「加速之前」的基准值（不走比例乘法，避免浮点误差累积）。
  // 未加速时调用是安全的空操作（`Server_StopSpeedBoost` 可以被重复发）。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "SprintBoost")
  void ResetMaxSpeed();

  // 服务器侧改写「基准速度」（例如装备 / 疲劳等系统永久改变了角色速度）：
  // **未被加速污染时**才允许写入，加速进行中调用会记一条 WFLOG_WARNING 并忽略。
  //
  // `InBaseMaxWalkSpeedCrouched < 0`（默认）表示蹲伏基准按原有比例
  // （`CrouchSpeedRatio`）推算；显式传入时会用新的「蹲伏 / 步行」比值更新这个比例。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "SprintBoost")
  void SetBaseMaxSpeed(float InBaseMaxWalkSpeed,
                       float InBaseMaxWalkSpeedCrouched = -1.0f);

  // 立即停掉加速并回到基准速度。语义上等价于 `ResetMaxSpeed()`，
  // 留这个名字是给蓝图状态机读起来更清楚（「打断冲刺」）。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "SprintBoost")
  void StopSpeedBoost() { ResetMaxSpeed(); }

  // ===== 客户端 -> 服务器：加速请求 =====
  // 输入绑定通常写在蓝图里（模板的 Enhanced Input）：判定「长按」后发
  // `Server_StartSpeedBoost`，松开按键发 `Server_StopSpeedBoost`。
  // C++ 侧不重复绑定输入，避免与蓝图绑定叠加。
  // `_Validate` 只做廉价参数检查；真正的范围收敛在权威函数内部完成。

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "SprintBoost|RPC")
  void Server_StartSpeedBoost(float HoldThreshold, float TargetMaxSpeed,
                              float BoostDuration = -1.0f,
                              float BoostInterval = -1.0f);

  // 停止加速（未加速时调用无害，可放心重复发）
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "SprintBoost|RPC")
  void Server_StopSpeedBoost();

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

  // Called every frame（默认关掉，只有开了 `bDriveOwnerLocally` 才用）
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  // 加速曲线是否跑在本地（默认关）。
  //
  // 为什么默认关：`MaxWalkSpeed` 由 CharacterMovement 复制，权威端在服务器；
  // 客户端再算一套只会在收到复制值那一帧被覆盖，纯粹的浪费。
  // 但它对**单机 / 本地调试**和「想要本地立刻起速的竞速手感」是有用的，
  // 所以做成开关而不是彻底删掉。开启后 `OnSprintBoostUpdated` 也会在客户端每帧广播，
  // UI 不必再自己插值。
  //
  // ⚠️ 它必须在 protected 而不是 private：UHT 不允许在**私有**成员上用
  // `BlueprintReadOnly`（会报 "BlueprintReadOnly should not be used on private
  // members"）。想留在 private 就得去掉 BlueprintReadOnly。
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "SprintBoost|Advanced")
  bool bDriveOwnerLocally = false;

private:
  // ===== 加速的内部状态 =====
  // 全部只在权威端写入：客户端通过 CharacterMovement 的 MaxWalkSpeed 复制看到结果。
  struct FSprintBoostState {
    // 本次提升的起始速度（权威端开始提升那一刻的 MaxWalkSpeed）
    float StartSpeed = 0.0f;
    // 本次提升的目标速度
    float TargetSpeed = 0.0f;

    // 未被加速污染的基准 MaxWalkSpeed / MaxWalkSpeedCrouched，重置时精确回到它们
    float BaseMaxWalkSpeed = 0.0f;
    float BaseMaxWalkSpeedCrouched = 0.0f;
    // 上面两个基准值**是否已经捕获过**（只有 CaptureBaseMaxSpeed 会置 true）。
    //
    // 为什么必须有这个标志：`BaseMaxWalkSpeed` 的默认值就是 0.0，而 0 恰好是
    // 「角色完全走不动」的合法速度值 —— 光看数值无法区分「基准是 0」和
    // 「还没抓过基准」。任何在捕获之前写回基准值的代码都会把角色钉死在原地
    // （bug-027 就是这么发生的）。所以凡是要写回基准值的地方，先看这个标志。
    bool bBaseSpeedValid = false;
    // 蹲伏速度与步行速度的基准比例（BaseMaxWalkSpeedCrouched / BaseMaxWalkSpeed）：
    // 与基准速度一起在每次开始加速前抓取（CaptureBaseMaxSpeed）。加速时按这个比例
    // 同步缩放蹲伏速度，而不是把蹲伏抬到和步行一样快（那样加速中蹲下就没有任何代价了）。
    float CrouchSpeedRatio = 1.0f;

    // 提升已经持续的时间（秒）；进度 = ElapsedTime / BoostDuration
    float ElapsedTime = 0.0f;
    // 从起始速度线性升到目标速度需要的总时间（秒）
    float BoostDuration = 1.0f;
    // 上一次刷新时的世界时间（秒），用来算两次刷新之间的真实时间增量。
    // ⚠️ 不能用 `GetTimerElapsed(Handle)`：它是按 Rate 反推出来的
    // （`TimerManager.cpp:795-812`），掉帧补触发（CallCount > 1）时会算出负值，
    // ElapsedTime 会倒退。
    float LastUpdateTime = 0.0f;

    // 刷新用的定时器句柄（间隔模式是循环定时器，每帧模式是 next-tick 定时器）
    FTimerHandle TimerHandle;

    // 每次 StartSpeedBoost 自增的「代次」。OnSprintBoostUpdated 广播时蓝图可能
    // 重开 / 重置加速，定时器回调续挂 next-tick 之前靠它确认自己这一代没被替换掉，
    // 否则会出现两条定时器链同时跑（旧链的句柄被覆盖，谁也停不掉它）。
    uint32 Generation = 0;

    // 是否处于「加速中」——含已经到顶、定时器已停但速度仍保持的状态
    bool bBoostActive = false;
    // 刷新方式：true = 固定间隔的循环定时器；false = 每帧一次的 next-tick 链
    bool bIntervalTimer = false;
    // 已完成的「开始」广播是否还没被「结束」广播配对掉（保证 Started/Stopped 成对）
    bool bStartBroadcastPending = false;
  };

  FSprintBoostState BoostState;

  // 一次方向判定的结果快照。
  // 单独做一个结构体是因为它有**三个消费者**：方向门控（要不要收掉加速）、
  // 调试字符串（日志里要打出夹角）、蓝图查询（UI 显示冲刺条件）。
  // 如果让它们各自算一遍，三处实现迟早会漂移。
  struct FSprintDirectionInfo {
    // 移动输入方向与角色正前方的夹角（度，0..180）；无输入时为 0
    float InputAngleDegrees = 0.0f;
    // 本帧是否读到了有效的移动输入（长度 >= SprintDirectionInputThreshold）
    bool bHasMoveInput = false;
    // 该输入是否算「向前」。语义：门控**是否允许继续加速**
    // （无输入 / 判定关闭 / 非步行状态都算 true，见 EvaluateSprintDirection 的注释）
    bool bInputDirectionIsForward = true;
    // 人类可读的原因，供日志与调试串使用
    FString Reason;
  };

  // 权威安全的「方向是否算向前」判定。
  //
  // 读的是 `UCharacterMovementComponent::GetCurrentAcceleration()`，**不是**
  // `GetLastInputVector()`——原因见 `bSprintOnlyForward` 的注释（服务器上后者恒为 0）。
  //
  // 参考方向用角色正前方（`Owner->GetActorForwardVector()`）而不是控制旋转：
  // 本项目的相机就是角色朝向，要改成「以相机为参考」时把这里换掉即可。
  // （注意 `bUseControllerRotationYaw` 为 true 时 actor 的 yaw 已跟随控制旋转，
  //  换不换都一样；为 false 时 actor 朝向会跟着移动方向走，那反而更宽松。）
  //
  // 放行（返回 true）的四种情况，每一种都对应一个具体的误伤场景：
  //   1. `bSprintOnlyForward == false`：功能关着，不看方向。
  //   2. **没有移动输入**：站着不动按 Shift（先按 Shift 后按 W 是常见操作顺序，
  //      若在这里就拒绝，玩家必须先动起来才能起速，手感很差）。方向门控的意义是
  //      「移动时只允许向前」，不是「必须已经在动」。
  //   3. **非步行状态**（`MOVE_Falling` 等）：跳跃 / 下落中不该因为读不到移动输入
  //      就把加速收掉（UE 默认 `AirControl = 0.05`，空中按方向键几乎不产生 Acceleration）。
  //      落地回到步行后立即恢复判定。
  //   4. 没有移动组件 / 宿主：调用方会单独记 WARNING，这里只保证不误收。
  FSprintDirectionInfo EvaluateSprintDirection() const;

  // 定时器回调：按 ElapsedTime / BoostDuration 把 MaxWalkSpeed 从起始值插值到目标值，
  // 到顶即停表（不再空转），但速度保持到 ResetMaxSpeed 为止。
  // 每帧模式（bIntervalTimer == false）会在末尾重新挂一次 next-tick 定时器。
  void TickSprintBoost();
  // 只清定时器与刷新方式标志，不动 bBoostActive（到顶时需要保留「加速中」状态）
  void ClearSprintBoostTimer();
  // 把当前 MaxWalkSpeed / MaxWalkSpeedCrouched 记为基准值，并算出蹲伏比例
  // （仅在「未加速」时调用才有意义）
  void CaptureBaseMaxSpeed();
  // 权威端专用：把步行速度写到移动组件，并让蹲伏速度按基准比例跟随缩放
  void ApplyMaxWalkSpeed(float InMaxWalkSpeed, float Alpha);
  // 客户端本地驱动用的轻量插值：只更新 BoostAlpha 并广播，**不碰移动组件**
  void TickLocalBoostVisualOnly();

  // 已经广播过 OnSprintBoostStarted、但还没被 OnSprintBoostStopped 配对掉。
  // 用它保证 Started/Stopped 严格成对（订阅者不会收到「无对应的结束」）。
  // 注意只标记 Started 那一侧，Stopped 是瞬时事件，不需要单独记。
  void BroadcastBoostStarted();
  void BroadcastBoostStopped(const TCHAR *Reason);
};
