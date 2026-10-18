// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"
#include "structs/AttackMontageData.h"

#include "AttackComponent.generated.h"

class UWeaponAttackProfile;

// 攻击生命周期通知。
// 两个都是「本地广播」：服务器与每个客户端各自在自己的机器上收到。
// 之所以能这样，是因为蒙太奇播放被 Multicast_PlayAttackMontage 同步到了所有端
// （引擎默认**不**复制蒙太奇，见下方 Multicast_PlayAttackMontage 注释），
// 所以任何一端都能靠在本地收到的 OnMontageEnded 广播 OnAttackFinished。
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttackFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttackStarted);

/**
 * 攻击组件（服务器权威）。
 *
 * ## 权威边界一览
 *
 * | 内容 | 权威端 | 客户端 | 通道 |
 * |---|---|---|---|
 * | 起手 / 选段 / 连击排队与判定 `Attack()` | 服务器 | ✗ 调不动（Absorbed） | `Server_Attack` / `RequestAttack` |
 * | 伤害判定 `PerformDamageTrace()` | 服务器 | ✗ | 由动画通知在服务器触发 |
 * | 调参（`AttackDamage`/`AttackRange`…） | 服务器读 ini 后写入 | 只读（复制下来） | 类默认值兜底 + `COND_InitialOnly` |
 * | 攻击状态 `bIsAttacking` / 连击窗口 / 选段下标 / 在播蒙太奇 | 服务器写 | 只读（复制下来） | `DOREPLIFETIME_CONDITION(COND_SimulatedOnly)` |
 * | 蒙太奇播放 | 服务器发起 | 跟随（但不写状态） | `Multicast_PlayAttackMontage(蒙太奇 + Section)` |
 * | 特效 `PlayAttackEffects()` | 服务器发起 | 跟随 | `Multicast_PlayAttackEffects` |
 * | 取消攻击链 `CancelAttack()` | 服务器 | ✗（改调 `Server_CancelAttack`） | `Multicast_CancelAttack` |
 *
 * 判断规则：**会改变玩法状态的一律权威端执行**；纯表现（特效、音效、动画播放）
 * 用 Multicast 同步到所有端。权威函数都带 `BlueprintAuthorityOnly`，并且内部还有一道
 * `IsAuthoritativeForActorComponent(this)` 开发期门禁，让客户端误用立刻在日志里可见，
 * 而不是被引擎静默丢弃（`callspace = Absorbed`，单机看不出来、联机才失效）。
 *
 * ## 连击流程（关键：连击通知在蒙太奇**播放中途**发出）
 *
 * ```
 * 起手 Attack() ──► 播第 1 段，bIsAttacking = true（订阅者禁止移动输入）
 *                     │
 *   动画中途的连击通知 ─┴─► Server_NotifyComboWindow ──► bCanCombo = true（开窗）
 *                     │
 *   窗口内玩家再点一次 ─┴─► Server_Attack ──► **立刻切播下一段**（不等本段播完）
 *                     │                       交叉淡入衔接，bIsAttacking 保持 true
 *                     │
 *   没有下一段可播 ─────┴─► FinishAttackChain：复位 + OnAttackFinished（恢复移动）
 *
 *   跳跃打断（蓝图调用） ─┴─► Server_CancelAttack → CancelAttack
 *                            → Multicast_CancelAttack（各端停播蒙太奇）
 *                            → FinishAttackChain（复位 + 恢复移动输入）
 * ```
 *
 * 三条容易搞错的地方：
 *   1. **「本段正在播」不等于「不接受输入」**。曾经用一个 `bAttackMontageInProgress`
 *      在 `Attack()` 最前面直接 return，结果连击通知永远被拒（通知就是在播放中发的）。
 *   2. **连击不受 `AttackCooldown` 约束**，那是管「两次起手之间」的；用卡起手的冷却去
 *      卡连击窗口，会让开得早的窗口被无声拒绝。连击只用 `ComboMinInterval` 做极小节流
 *      （防被篡改的客户端在窗口里刷包）。
 *   3. **节流的单位是「窗口」，不是「蒙太奇段」**：绝对不要加「一段只收一次输入」的
 *      标志来防连点——起手那次输入之后整段都不会有新的按下事件来复位它，窗口一开
 *      所有点击全被拒，连击永远接不上（实测踩过，见 bug-022）。
 *      窗口每段都会被通知重开，天然就是「一次机会」。
 *   4. 切段会停掉上一段蒙太奇，引擎随即触发上一段的 `OnMontageEnded(bInterrupted=true)`；
 *      用 `bAdvancingCombo` 标记把它和「攻击链真的结束」区分开。
 *
 * ⚠️ `UActorComponent` 上没有 `HasAuthority()`（那是 `AActor` 的方法，直接写会 C3861），
 * 组件里一律用 `Utils/WildforgeAuthority.h` 的 `IsAuthoritativeForActorComponent(this)`。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API UAttackComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  UAttackComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  // ===== 可配置参数（类默认值 = 基线 + 兜底；ini 可覆盖，随复制下发）=====
  // 这一节现在带 `Replicated`（`COND_InitialOnly`：出生束里带一次，之后零开销）：
  //   * **权威端**在 BeginPlay 用 UAttackComponentSettings 的 ini 覆盖写进它们
  //     （只有显式勾了 `bOverride_XXX` 的项才写，见 ApplyGameplaySettingsOverrides）；
  //   * 客户端不读 ini，靠这条复制通道拿到服务器那份值——两端必须一致，
  //     否则客户端预测用的参数与服务器不同步（bug-029 / bug-031 那一类橡皮筋）。
  // 没勾 override 时值就是这里的类默认值，**每个类（Boss / 小怪）可以不一样**。
  // ⚠️ 客户端不要在本端写这些字段：写了不会同步回服务器，只会让自己与服务端不一致。

  // 能不能攻击的总开关（眩晕 / 缴械 / 死亡等状态把它关掉）。
  // 服务器侧的门：`Attack()` 第一件事就是查它，关掉时起手与连击都不生效。
  // 想给客户端 UI 看到「现在不能攻击」，就让它走复制——它现在是普通 UPROPERTY，
  // 客户端自己改只影响本地副本，服务器的判定不受影响。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  bool bCanAttack = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Attack")
  float AttackCooldown = 0.5f;

  // 连击接招的最小间隔（秒）：两个连击段起始时间的最小距离。
  // 第 1 段起手时记 LastAttackTime，接招时若 Now - LastAttackTime < 本值就拒绝。
  // 它**只**用来防「被篡改的客户端在同一个窗口里刷出几十个请求、一口气打完整条
  // 连招」；正常手感由连击通知在动画里的位置决定，所以这个值要**明显小于**两面
  // 窗口之间的间隔（作者把连击通知摆在哪一帧），否则会把正常连击也拒掉。
  // 实测参考：连击通知大约在第 0.7s 处，取 0.15 很安全。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Attack",
            meta = (ClampMin = "0", UIMin = "0"))
  float ComboMinInterval = 0.15f;

  // 连击切段的交叉淡入时长区间（秒）：切段时取「上一段已经播了多久」，再夹到
  // [ComboBlendInMinTime, ComboBlendInMaxTime]。硬切会看到抽帧，所以至少给一点淡入；
  // 上限防止连击窗口摆得靠后时淡入过长、动作发肉（实测参考：窗口约在第 0.7s 处，
  // 夹在 0.05~0.2s 之间手感最好）。这是**表现参数**，两端各自用同一份类默认值。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Attack",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float ComboBlendInMinTime = 0.05f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Attack",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float ComboBlendInMaxTime = 0.2f;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Attack")
  float AttackDamage = 10.0f;

  // 角色默认（徒手）招式表。由 `UCharacterAdaptor::AttackMontageList` 在每个端
  // BeginPlay 应用；装了武器后由 `WeaponAttackProfile` 覆盖。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  TArray<FAttackMontageData> AttackMontageList;

  // ===== 武器招式（服务器权威）=====
  //
  // 不同武器 = 不同的 `UWeaponAttackProfile` 蓝图子类（类引用），拾取时由武器
  // 在自己的服务器分支调用 `SetWeaponAttackProfile()` 写进本组件。**不要**给武器
  // actor 挂 AttackComponent（它没有 owning connection，Server RPC 会被丢弃；
  // 蒙太奇也必须播在角色身上，见 bug-093）。
  //
  // 这个引用复制给所有端：模拟代理也能在调试/UI 里看到当前武器；
  // 但玩法判定（推进哪一段）只在服务器读它。

  // 当前武器的招式表**类**；空 = 用 `AttackMontageList`（徒手默认招式）。
  // 用 `TSubclassOf`（类引用）而不是对象引用：内容浏览器里「Blueprint Class →
  // WeaponAttackProfile」建出来的是**类资产**，`TObjectPtr<T>` 属性选不到它；
  // 类引用则可以直接指向这个蓝图类，数据读它的 CDO（与 `UCharacterAdaptor`
  // 在角色蓝图里指定适配器蓝图类完全同一套约定）。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly,
            ReplicatedUsing = OnRep_WeaponAttackProfile, Category = "Attack|Weapon")
  TSubclassOf<UWeaponAttackProfile> WeaponAttackProfile;

  // 设置当前武器招式（由武器拾取流程在服务器调用）。
  // 正在攻击时会先取消旧攻击链——段位下标是按旧列表推进的，直接换表会越界
  // 或播到错误的段。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attack|Weapon")
  void SetWeaponAttackProfile(
      TSubclassOf<UWeaponAttackProfile> InProfileClass);

  // 清空武器招式（丢弃 / 切换武器），回退到角色默认招式。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attack|Weapon")
  void ClearWeaponAttackProfile();

  // 当前生效的招式表：武器配置非空且列表非空 → 武器列表；否则 → 默认列表。
  // 非 UFUNCTION（返回 const 引用）；UI / 调试 C++ 代码可直接调用。
  const TArray<FAttackMontageData> &GetEffectiveAttackMontageList() const;

  // ===== 伤害检测参数（服务器权威：检测只在服务器跑，客户端改了也没用）=====

  // 检测射程（cm）：从角色胸口沿朝向往前扫这么远
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Attack|Damage",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
  float AttackRange = 200.0f;

  // 检测半径（cm）：0 = 细线段（第一人称近战手感更精准）；> 0 = 球形扫掠
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Attack|Damage",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
  float AttackTraceRadius = 0.0f;

  // 检测起点相对角色原点的 Z 偏移（cm）：原点在胶囊体中心，胸口大约 +30
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Attack|Damage")
  float AttackTraceHeightOffset = 30.0f;

  // 检测用的碰撞通道。默认 `ECC_Visibility` 与引擎的视线检测一致：
  // 「能被视线看到的东西」就能被打到，不需要额外配置自定义通道。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Attack|Damage")
  TEnumAsByte<ECollisionChannel> AttackTraceChannel = ECC_Visibility;

  // ===== 状态（服务器写、客户端只读，见 Replicated）=====

  // 是否正在攻击（客户端用它做 UI / 输入门控，不参与玩法判定）
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Attack")
  bool bIsAttacking = false;

  // 连击窗口是否敞开：由动画通知 `Server_NotifyComboWindow` 在蒙太奇播放中途打开，
  // 收到一次连击输入后关闭。**只有服务器读取并写入**。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Attack")
  bool bCanCombo = false;

  // 当前选中的蒙太奇 / Section 下标。**只允许服务器推进**，客户端只读：
  // 它决定「打哪一段、什么时候开连击窗口」，客户端能改就等于能自选攻击段。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Attack")
  int32 MontageSectionIndex = 0;

  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Attack")
  int32 AttackMontageIndex = 0;

  // 上一次攻击开始的世界时间（秒）。**只在服务器上读写**（冷却判定的依据），
  // 这里传给客户端只是为了调试与 UI 显示——复制它不会造成任何玩法决策依赖客户端。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Attack")
  float LastAttackTime = 0.0f;

  // 当前这一段攻击实际在播的蒙太奇。
  // 为什么单独复制它、而不是用 AttackMontageIndex 去查表：
  // 服务器推进下标（`AttackMontageIndex++`）与实际播放之间存在时序差，
  // 客户端在 OnMontageEnded 里可能已经拿到「下一段」的下标，于是把本段结束
  // 误判成「不是我播的那段」而丢掉 OnAttackFinished（角色卡住不能移动）。
  // 直接记「在播什么」与下标推进解耦，跨蒙太奇连击时也稳。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Attack")
  TObjectPtr<UAnimMontage> ActiveAttackMontage;

  // ===== 通知 =====
  // 两段式生命周期：OnAttackStarted（蒙太奇真的要播了）/ OnAttackFinished（蒙太奇结束）。
  // 订阅者（如 APlayerCharacter 禁止/恢复移动输入）只认这两个事件，不需要知道蒙太奇细节。

  UPROPERTY(BlueprintAssignable, Category = "Attack")
  FOnAttackStarted OnAttackStarted;

  UPROPERTY(BlueprintAssignable, Category = "Attack")
  FOnAttackFinished OnAttackFinished;

  // ===== 查询（纯读，任何端都能安全调用）=====

  // 攻击状态的一行快照，专供日志/调试用（连击排查时每个判定点都会打它）。
  // 之所以做成成员函数而不是自由函数：这几个状态里有私有成员，
  // 自由函数拿不到（会报 is a private member）。
  FString GetAttackStateDebugString() const;

  // 连击窗口是否敞开（供 UI 显示连击提示）
  UFUNCTION(BlueprintPure, Category = "Attack",
            meta = (BlueprintThreadSafe))
  bool IsComboWindowOpen() const { return bCanCombo; }

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

  // 武器招式表复制到达（客户端）。只记日志：客户端播放哪一段完全由
  // Multicast 带来的资产决定，这里不写任何玩法状态。
  UFUNCTION()
  void OnRep_WeaponAttackProfile();

  // 读取 UAttackComponentSettings 的 ini 覆盖并写进本组件。
  // **只在权威端执行**（客户端那份值由上面那些 `COND_InitialOnly` 属性复制下来）；
  // 没有勾任何 override 时是安全空操作，只打一条「用类默认值」的日志。
  void ApplyGameplaySettingsOverrides();

  // 把指定蒙太奇的指定 Section 播出来（服务器权威入口，带门禁）。
  // **只负责播放**：不改段位下标、不广播（广播与下标推进由调用方负责），
  // 所以它可以在「起手」与「连击切段」两条路径上复用。
  //
  // 参数直接是资产 + Section 名（而不是招式表下标）：服务器用当前武器配置
  // 解析出这一段，客户端收到的就是同一个资产，不依赖两端列表一致。
  //
  // `InFadeInTime` > 0 时用 `Montage_PlayWithBlendIn` 与上一段做交叉淡入
  // （连击立刻切段时用，硬切会看到抽帧）；0 = 硬切（起手）。
  //
  // 返回是否真的播了。
  UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category = "Attack")
  bool PlayAttackMontage(UAnimMontage *InMontage, FName InSectionName,
                         float InFadeInTime);

  // 服务器在整条攻击链结束时复位攻击状态（含关掉连击窗口与清掉输入标记）。
  // 连击切段（段与段之间）**不**走这里——那条路径要保持 bIsAttacking = true。
  UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category = "Attack")
  void ResetAttackState();

  // 动画通知回调：判断结束的是不是「我们正在播的那一段」。
  // 所有端都会触发（蒙太奇被 Multicast 同步），但状态复位与 OnAttackFinished 的
  // 语义在服务器与客户端都是成立的，所以这里不加权威门禁。
  UFUNCTION()
  void OnAttackMontageEnded(UAnimMontage *Montage, bool bInterrupted);

  // 兜底：动画异常（蒙太奇没触发 OnMontageEnded）时强制收尾，避免角色永久卡在
  // 「攻击中不能移动输入」。时长按当前蒙太奇的剩余时间估算。
  void HandleAttackTimeout();
  // 指定蒙太奇的时长（秒）；取不到时返回 1.0 作为兜底
  float GetMontagePlayLength(int32 InMontageIndex) const;

  // PlayAttackMontage 的无门禁内核：服务器与客户端都走它（客户端只播动画、
  // 不写任何玩法状态）。权威入口与 Multicast 都转发到这里，避免逻辑写两份。
  bool PlayAttackMontageInternal(UAnimMontage *InMontage, FName InSectionName,
                                 float InFadeInTime);

  // 把玩法状态推进到「下一段」（纯逻辑，不播动画）：段用完就换下一个蒙太奇。
  // 只在服务器、且本段确实播出去之后调用。
  void AdvanceAttackSection();

  // 连击接招：**立刻**切播下一段（不等当前段落播完），并保持 bIsAttacking
  // （移动锁不解）。`PrevSegmentStartTime` 用来算交叉淡入时长。
  // 返回是否真的接上了（没有下一段可播时返回 false = 连招打完）。
  bool AdvanceAttackChain(float PrevSegmentStartTime);

  // 整条攻击链结束：复位状态 + 清定时器 + 广播 OnAttackFinished（订阅者据此恢复移动输入）。
  void FinishAttackChain(const TCHAR *Reason);

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  // ===== 服务器权威函数（BlueprintAuthorityOnly + 内部开发期门禁）=====
  // 客户端直接调用会被引擎静默丢弃（callspace = Absorbed），请改调下方 Server_* / Request_*。

  // 发起 / 接续一次攻击。
  //
  // 行为分两种情况（两种情况都先受总开关 `bCanAttack` 约束）：
  //   * 空闲时：起手。受 `AttackCooldown`（上一次起手到现在的时间）约束。
  //   * 攻击中且连击窗口敞开时（窗口由动画通知在蒙太奇**播放中途**打开）：
  //     立刻切播下一段动画。**不等当前这段播完**——窗口一开点击就该出下一招。
  //     只受 `ComboMinInterval` 的极小节流约束，不受 `AttackCooldown` 约束。
  //
  // `bIsInputDriven` = 来自玩家输入（默认 true）：每段蒙太奇只接受**一次**输入，
  // 必须松开再按才会接下一招；服务器/蓝图脚本驱动时传 false 可跳过这个限制。
  //
  // 返回是否被接受（起手成功 或 已切到下一段）。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Attack")
  bool Attack(bool bIsInputDriven);

  // 伤害判定。**必须在服务器执行**（客户端能触发的话就是自造伤害）。
  // 由动画通知 `UAnimNotify_AttackHit` 在服务器侧调用。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Attack")
  void PerformDamageTrace();

  // 打开连击窗口。由动画通知 `Server_NotifyComboWindow` 在服务器侧调用；
  // 也可以由服务器侧的蓝图状态机直接调（如「翻滚取消后仍可接一招」）。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attack")
  void OpenComboWindow();

  // 取消当前攻击链（跳跃打断等）。**服务器执行**：先 Multicast 让所有端停播
  // 攻击蒙太奇，再各端 FinishAttackChain（复位状态 / 广播 OnAttackFinished）。
  // 输入接线由蓝图负责：客户端蓝图调 `Server_CancelAttack()`，不要在这里写死
  // 某个按键 / 动作的判断。
  // 不在攻击中时是安全空操作（返回 false）。
  //
  // 之所以要单独做一条「取消」而不是直接停蒙太奇：蒙太奇被其他系统停掉时，
  // 各端的 OnAttackMontageEnded 仍会走一遍正常收尾；但取消攻击链还需要
  // 主动推进「玩法状态复位 + 移动输入解锁」，且不能等淡出结束才生效。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attack")
  bool CancelAttack();

  // 播放攻击表现（特效 / 音效 / 镜头震动）。
  // 在服务器上调用会经 Multicast 同步到所有端；客户端也可以本地调用（纯表现）。
  // 蓝图覆写同名事件即可实现具体效果。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Attack")
  void PlayAttackEffects();

  // ===== 客户端 -> 服务器：攻击请求 =====
  // 输入绑定通常写在蓝图里（Enhanced Input 的 IA_Attack），C++ 侧不重复绑定。
  // `_Validate` 只做廉价检查（这里没有参数，恒为 true）；
  // 真正的冷却 / 连击窗口 / 排队与段位推进全部在服务器内部判定。

  // 请求执行一次攻击（起手或连击）。被拒绝时无任何副作用，可放心连点。
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Attack|RPC")
  void Server_Attack();

  // 请求打开连击窗口（由动画通知在蒙太奇中途调用）。服务器会校验「当前确实在攻击中」，
  // 防止客户端在没出招时凭空打开连击窗口。
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Attack|RPC")
  void Server_NotifyComboWindow();

  // 请求取消当前攻击链（跳跃打断走这里）。无参数，_Validate 恒真；
  // 服务器侧只在 `bIsAttacking` 为真时真正取消。
  // 与 `Server_Attack` 一样：Server RPC 在服务器上会就地同步执行，
  // 单机 / 监听服务器 / 联机走同一条路径。
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Attack|RPC")
  void Server_CancelAttack();

  // 语义化别名，与 `Server_Attack` 同一个东西：输入层写 RequestAttack 读起来更清楚。
  UFUNCTION(BlueprintCallable, Category = "Attack|RPC")
  void RequestAttack() { Server_Attack(); }

  // 连击窗口关闭（攻击链结束 / 被打断）。由玩家输入驱动的连击在服务器侧读取，
  // 玩家自己不需要调用它；这里暴露给服务器蓝图做取消类逻辑。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attack")
  void CloseComboWindow();

  // ===== 服务器 -> 所有端：表现同步 =====
  // 引擎默认**不复制蒙太奇播放**（`FRepRootMotionMontage` 只复制 RootMotion 那一段的状态），
  // 所以想让客户端也看到攻击动画，必须显式 Multicast。
  //
  // 参数直接把**蒙太奇资产 + Section 名**带过去，客户端不靠本地的招式表下标
  // 反查：
  //   * RPC 与属性复制谁先到不保证，靠读下标会让客户端偶尔播错段；
  //   * 不同武器各有各的招式表，客户端本地那份可能还没切过来（拾取瞬间），
  //     下标在两张表里指向的蒙太奇可以完全不同。
  // 把资产本身发过去，客户端播的一定是服务器正在打的那一段。
  //
  // ⚠️ 这两个 `_Implementation` **不能加「非权威端就忽略」的门禁**：Multicast 在每个端
  // 都会执行，客户端那一次是**正常接收**；门禁会让客户端永远看不到攻击动画与特效。
  // 引擎规则：Multicast 在服务器返回 `Local | Remote`，在客户端只返回 `Local` 且不再转发
  // （`Actor.cpp:5500-5519`；被标成 `BlueprintAuthorityOnly` 才是 Absorbed，:5429-5432），
  // 所以「客户端自己调用」只作用于本机，没有刷屏风险。实现里只做表现、不写玩法状态。

  UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Attack|RPC")
  void Multicast_PlayAttackMontage(UAnimMontage *InMontage, FName InSectionName,
                                   float InFadeInTime);

  UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Attack|RPC")
  void Multicast_PlayAttackEffects();

  // 服务器 -> 所有端：停止攻击蒙太奇并收尾。
  // 与上面两条 Multicast 同一套规则：**不要**在这里加「非权威端就忽略」的门禁
  // ——每个端都要停掉自己本地的蒙太奇、广播 OnAttackFinished 解锁移动输入。
  // 先清 `ActiveAttackMontage` 再 `Montage_Stop`，避免淡出结束后延迟到达的
  // `OnMontageEnded` 再把收尾走第二遍。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Attack|RPC")
  void Multicast_CancelAttack();

private:
  // 本端是否真的广播过 OnAttackStarted（= 本端确实把攻击蒙太奇播起来了）。
  // Started / Finished 必须**严格配对**：蒙太奇没播起来时不会广播 Started，
  // 收尾时也就不能广播 Finished——否则订阅者会「解锁一把没落过的锁」，
  // 在日志里留下无配对的 WARNING（同类问题见 bug-030）。
  // 另外，取消攻击时用它判断本端要不要做收尾广播（没播过就没有锁可解）。
  // **不复制**：这是每端各自的表现层状态。
  bool bLocalAttackPresentationActive = false;

  // 正在为连击切段（我们自己停掉了上一段蒙太奇）。
  // 引擎在 Montage_Play 新蒙太奇时会触发上一段的 OnMontageEnded(bInterrupted=true)，
  // 这个标记让回调知道「那是我们自己干的」，不要当成攻击链结束。**不复制**。
  bool bAdvancingCombo = false;

  // 攻击超时兜底定时器（OpenComboWindow 时挂上，攻击链结束/组件销毁时清掉）
  FTimerHandle AttackTimeoutHandle;
};
