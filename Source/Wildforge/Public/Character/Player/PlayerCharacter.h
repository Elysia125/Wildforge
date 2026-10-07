// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "../BaseCharacter.h"
#include "Character/Components/AttackComponent.h"
#include "Character/Components/BlinkComponent.h"
#include "Character/Components/LandRollComponent.h"
#include "Character/Components/SprintBoostComponent.h"
#include "ItemSystem/Components/PlayerInventory.h"

#include "PlayerCharacter.generated.h"

/**
 * 玩家角色：把玩家专用的「能力组件」装配在一起。
 *
 * ## 组件分工（都是 UActorComponent，各自负责自己的权威与复制）
 *
 * | 组件 | 能力 | 权威状态 | 客户端入口 |
 * |---|---|---|---|
 * | `UPlayerInventory` | 背包 | `Slots`（`COND_OwnerOnly`） | `Server_*` |
 * | `UAttackComponent` | 攻击 / 连击 | `bIsAttacking` / 连击窗口 / 段位下标 | `Server_Attack` |
 * | `ULandRollComponent` | 翻滚 | `bIsRolling` | `Server_LandRoll` |
 * | `USprintBoostComponent` | 加速（长按 Shift） | `bBoostActive` + `CharacterMovement::MaxWalkSpeed` | `Server_StartSpeedBoost` |
 * | `UBlinkComponent` | 闪现（点按 Shift） | `LastBlinkTime` / `BlinkCount` | `Server_Blink` |
 *
 * ## 攻击 / 翻滚期间的移动门控在这里落地
 *
 * 订阅攻击组件的 `OnAttackStarted`（禁止移动）/ `OnAttackFinished`（恢复移动）与
 * 翻滚组件的 `OnLandRollStarted` / `OnLandRollFinished`，不对组件的内部逻辑做任何
 * 假设——组件只负责广播「开始了 / 结束了」。
 *
 * ### 两种强度：硬锁与软锁（「边跑边滚」手感的关键）
 *
 * | 来源 | 强度 | 对移动组件做了什么 |
 * |---|---|---|
 * | `MovementLockAttack` | **硬锁** | `StopMovementImmediately()` + `DisableMovement()`（MOVE_None） |
 * | `MovementLockLandRoll` | **软锁** | 什么都不做（只记账 / 复位加速 / 供 UI 查询） |
 *
 * 翻滚为什么必须是软锁（踩过的坑）：
 *
 *   * `MOVE_None` 让 `UCharacterMovementComponent::PerformMovement` 在入口直接 return，
 *     并**显式丢弃根运动**（`CharacterMovementComponent.cpp:2716-2734`：
 *     `RootMotionParams.Clear()` / `CurrentRootMotion.Clear()`）。翻滚的位移本来就该由
 *     蒙太奇根运动或蓝图驱动，硬锁等于把位移一起丢掉 → 动作变成原地滚。
 *   * 硬锁还会 `StopMovementImmediately()`，把跑动中的速度直接清零 → 边跑边滚变成急停。
 *   * 硬锁只在服务器落（翻滚的 Started 由服务端权威路径广播），客户端那份没有被锁，
 *     于是服务器原地不动、客户端继续以跑速前进，最后被位置纠正拉回来 = 橡皮筋。
 *
 * 结论：**要禁止输入，不要关移动组件**。翻滚期间是否响应移动输入，属于动画状态机 /
 * 蓝图的门控；`CharacterMovement` 一旦关掉，连动画自带的位移都一起没了。
 *
 * ### 移动锁按「来源」记账，不是布尔、也不是计数
 *
 * 用一个 `TSet<FName>` 记录**谁**正持有锁（`MovementLockAttack` / `MovementLockLandRoll`），
 * 再用第二个集合 `MovementDisablingHolders` 记录其中**要求禁用移动组件**的那部分
 * （硬锁）。四条不变式：
 *
 *   1. **第一个持有者落锁、最后一个持有者解锁**：软锁全程不碰移动组件；
 *      硬锁在「硬锁持有者」集合 0→1 时才 `DisableMovement`，回到 0 才恢复移动模式。
 *      攻击与翻滚的 Started/Finished 会**交错**到达（翻滚中攻击、攻击中翻滚被取消），
 *      布尔标记下先结束的一方会把还在持续的另一方一起放开——那种 bug 不崩不报错，
 *      只表现为「翻滚没结束就能走」。
 *   2. **同一来源重复落锁幂等**：`Set` 的重复 Add 是空操作。这一条是必须的——
 *      攻击组件在**每次连击切段**时都会再广播一次 `OnAttackStarted`，
 *      一次 3 段连击会有 3 个 Started 但只有 1 个 Finished。用计数就会泄漏
 *      （锁数停在 2，角色永久不能动），用集合则天然幂等。
 *   3. **持有锁的组件必须与角色同生共死**：组件被销毁后不会再广播 Finished，
 *      锁会永久挂在集合里。当前满足（组件都是构造函数里 CreateDefaultSubobject 的），
 *      详见下方私有成员的说明；运行时移除组件必须先主动解锁。
 *
 * 为什么门控放在角色而不是组件里：**移动模式是本地瞬时状态**
 * （`CharacterMovement` 的 `MovementMode`），等服务器复制再禁用会明显发飘；
 * 事件是每端各自本地广播的，所以两端都能就地立即响应。
 */
UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable)
class WILDFORGE_API APlayerCharacter : public ABaseCharacter {
  GENERATED_BODY()
private:
  UPROPERTY(BlueprintGetter = GetInventory, Category = "Items")
  TObjectPtr<UPlayerInventory> Inventory;

  // 攻击组件（由构造函数创建，随宿主 Actor 复制）
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attack",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UAttackComponent> AttackComponent;

  // 翻滚组件（由构造函数创建，随宿主 Actor 复制）
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LandRoll",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<ULandRollComponent> LandRollComponent;

  // 加速组件（长按 Shift）：原先写在 ABaseCharacter 上的加速逻辑，现已独立成组件。
  // **旧蓝图节点需要重连到 `GetSprintBoostComponent()`**。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "SprintBoost",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<USprintBoostComponent> SprintBoostComponent;

  // 闪现组件（点按 Shift）：原 `ABaseCharacter::BlinkForward` / `Server_Blink`。
  // **旧蓝图节点需要重连到 `GetBlinkComponent()`**。
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Blink",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UBlinkComponent> BlinkComponent;

  // 禁用移动前的移动模式，硬锁全部释放后用来恢复（对应「启动移动」操作）。
  // 只在本机本地读写，不复制：恢复的判断依据是 CharacterMovement
  // 自己复制的移动模式，所以两端各自维护一份本地缓存即可。
  //
  // ⚠️ 只在**硬锁**把移动组件真的禁掉的那一刻写一次（见 bMovementDisabledByLock），
  // 否则会把 MOVE_None 当成「之前的模式」记下来，之后就再也恢复不回去了。
  TEnumAsByte<EMovementMode> MovementModeBeforeLock = MOVE_Walking;

  // 当前**谁**正持有「禁止移动」锁（软锁与硬锁都记在这里）。
  //
  // 为什么是「按来源记账的集合」而不是布尔或计数（两个都被否过）：
  //   * 布尔（`bMovementLockedBy…`）无法区分持有者，攻击结束会把翻滚的锁一起放掉；
  //   * 纯计数（`int32 MovementLockCount`）能区分数量但不能区分**来源**，
  //     重复事件（连击切段重复广播 `OnAttackStarted`）会累积泄漏，
  //     一旦 Started/Finished 不成对，锁数就永远回不到 0（角色永久不能动）。
  //   * 集合同时解决两者：重复 Add 幂等；集合非空 = 还有人在锁着。
  //     而且日志里能直接打出「当前被谁锁着」，排查时一眼看到是谁没释放。
  //
  // 不复制：移动模式本身是本地瞬时状态，各端各自维护一份即可（见类注释）。
  TSet<FName> MovementLockHolders;

  // MovementLockHolders 的子集：其中**要求禁用移动组件**（硬锁）的那些来源。
  // 软锁来源也在 MovementLockHolders 里，但不在这里。
  //
  // 为什么要单独一个集合：恢复移动模式的判据必须是「硬锁持有者是否已清空」，
  // 而不是「全部持有者是否已清空」。反例：翻滚（软）先落锁、攻击（硬）后落锁，
  // 攻击结束时翻滚还在滚——用全量集合判断就会因为「还有人持有」而不恢复移动，
  // 于是角色在剩下的翻滚时间里继续被 MOVE_None 卡住（原本想要的软锁反而变硬）。
  TSet<FName> MovementDisablingHolders;

  // 本端的移动组件当前是否**是被这套锁禁用掉的**：为真时 MovementMode 一定是
  // MOVE_None，且 MovementModeBeforeLock 是可信的（可以拿它恢复）。
  // 只用它来决定「要不要恢复移动模式」——不靠 `MovementMode == MOVE_None` 反推，
  // 因为 MOVE_None 也可能是别的系统（布娃娃 / 死亡 / 过场）设的，那种情况不该由我们恢复。
  bool bMovementDisabledByLock = false;

  // ===== 移动锁的来源标识 =====
  // 用 FName 常量而不是裸字面量：写错名字会变成一个「新来源」而不会报错，
  // 那种静默走偏很难查，所以统一从这里取。
  static const FName MovementLockAttack;
  static const FName MovementLockLandRoll;

  // 移动锁持有者的一行快照（日志用），空集合返回 "无"
  FString DescribeMovementLockHolders() const;

  // 硬锁持有者的一行快照（日志用），空集合返回 "无"
  FString DescribeMovementDisablingHolders() const;

  // 落锁时的加速复位：禁止移动期间把最大速度还原到基准值，免得解锁后带着加速滑。
  // 抽成函数是因为软锁与硬锁两条路径都要做，而且日志措辞不同（见实现）。
  void ResetSprintBoostOnMovementLock(FName Source);

  // ⚠️ 维护移动锁的前置条件：**持有锁的组件必须与角色同生共死**。
  //
  // 因为组件被销毁后不会再广播 Finished，那份 FName 就会永久留在
  // MovementLockHolders 里 = 角色永久不能移动。当前这套实现满足这个条件：
  // 攻击 / 翻滚组件都是本类构造函数里 CreateDefaultSubobject 出来的，
  // 生命周期与角色完全一致（角色没了 → 整个 Character 和锁一起消失）。
  //
  // 所以**不要在运行时 RemoveComponent / DestroyComponent 掉持有锁的组件**
  // （例如「卸下装备就移除攻击组件」这种设计）。真要做，必须先调
  // ReleaseMovementLock(该来源)，或者给该组件加一个销毁通知委托再解。
  // 新增持有移动锁的能力时同样遵守这一条。

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;

  // 组件销毁 / 角色销毁时退订，避免回调打到半销毁对象上
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  // 攻击开始：落**硬锁**（禁用移动组件）
  UFUNCTION()
  void HandleAttackStarted();

  // 攻击结束：硬锁释放（若没有别的硬锁持有者，恢复移动模式）
  UFUNCTION()
  void HandleAttackFinished();

  // 翻滚开始：落**软锁**（只记账，不碰移动模式与速度，见类注释）
  UFUNCTION()
  void HandleLandRollStarted();

  // 翻滚结束：软锁释放（只会更新记账，不会去恢复移动模式）
  UFUNCTION()
  void HandleLandRollFinished();

public:
  // Sets default values for this character's properties
  APlayerCharacter();

  UFUNCTION(BlueprintPure, Category = "Items", meta = (BlueprintThreadSafe))
  UPlayerInventory *GetInventory() const { return Inventory.Get(); }

  UFUNCTION(BlueprintPure, Category = "Attack", meta = (BlueprintThreadSafe))
  UAttackComponent *GetAttackComponent() const { return AttackComponent.Get(); }

  // 翻滚组件：旧蓝图里直接引用 `LandRollComponent` 变量的节点改用它（或继续用变量）
  UFUNCTION(BlueprintPure, Category = "LandRoll", meta = (BlueprintThreadSafe))
  ULandRollComponent *GetLandRollComponent() const {
    return LandRollComponent.Get();
  }

  // 加速组件：加速的全部权威函数与 RPC 都在它身上
  // （`Server_StartSpeedBoost` / `Server_StopSpeedBoost` / `ResetMaxSpeed` …）
  UFUNCTION(BlueprintPure, Category = "SprintBoost",
            meta = (BlueprintThreadSafe))
  USprintBoostComponent *GetSprintBoostComponent() const {
    return SprintBoostComponent.Get();
  }

  // 闪现组件：`Server_Blink` / `Server_BlinkForward` / `BlinkForward` 都在它身上
  UFUNCTION(BlueprintPure, Category = "Blink", meta = (BlueprintThreadSafe))
  UBlinkComponent *GetBlinkComponent() const { return BlinkComponent.Get(); }

  // ===== 移动锁（供能力组件 / 它们的所有者调用）=====

  // 落锁：`Source` 加入持有者集合。同一来源重复调用幂等（返回 false 表示这次没做任何事）。
  //
  // `bDisableMovement` 决定强度：
  //   * `true`（硬锁，攻击用）：第一个硬锁持有者会 `StopMovementImmediately()` +
  //     `DisableMovement()`，把角色钉住（MOVE_None）。⚠️ 硬锁会丢弃根运动，
  //     不要让靠根运动位移的动作（翻滚 / 位移技）用硬锁。
  //   * `false`（软锁，翻滚用）：只记账 + 复位加速，**完全不碰移动组件**，
  //     位移留给蒙太奇根运动 / 蓝图，边跑边滚的惯性得以保留。
  bool ApplyMovementLock(FName Source, bool bDisableMovement);

  // 解锁：`Source` 移出集合。只有当**硬锁**持有者清空、且移动组件确实是被这套锁
  // 禁掉的（bMovementDisabledByLock）时才恢复移动模式。
  // 返回 false 表示 `Source` 并不持有锁（说明 Started/Finished 不成对，会记 WARNING）。
  bool ReleaseMovementLock(FName Source);

  // 是否有任意来源（软锁或硬锁）持有锁。注意：软锁为真时角色**仍然能移动**，
  // 它表达的是「有动作占用了角色」，不是「动不了」——判断能不能动请用
  // IsMovementDisabled()。
  UFUNCTION(BlueprintPure, Category = "Movement Lock",
            meta = (BlueprintThreadSafe))
  bool IsMovementLocked() const { return MovementLockHolders.Num() > 0; }

  // 移动组件当前是否**真的**被这套锁禁用着（硬锁生效中）。
  UFUNCTION(BlueprintPure, Category = "Movement Lock",
            meta = (BlueprintThreadSafe))
  bool IsMovementDisabled() const { return bMovementDisabledByLock; }

  // 当前持有者集合的字符串快照，例如 "LandRoll"；空集合返回 "无"。
  // 排查「角色为什么不能动」时先看它。
  UFUNCTION(BlueprintPure, Category = "Movement Lock",
            meta = (BlueprintThreadSafe))
  FString GetMovementLockHoldersDebugString() const {
    return DescribeMovementLockHolders();
  }
};
