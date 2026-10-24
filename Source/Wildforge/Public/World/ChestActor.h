// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/Actor.h"
#include "ItemSystem/Enums/ChestLevel.h"
#include "ItemSystem/Enums/ContainerAccessMode.h"

#include "ChestActor.generated.h"

class APlayerController;
class UChestWindowWidget;
class UStorageContainer;

/**
 * 可交互宝箱：等级 + 世界容器 + 开箱表现 + 宝箱窗口。
 *
 * ## 职责划分
 *
 * - **等级**（`ChestLevel`）放在 Actor 上，不放在 `UStorageContainer`：等级同时
 *   驱动掉落稀有度、动画、窗口配色，属于「宝箱」这个概念；容器组件保持通用，
 *   尸体、熔炉、掉落箱等世界容器可以复用它而不被宝箱规则污染。
 * - **存储**由 `Container`（`UStorageContainer`）负责；内容 `COND_None` 复制给
 *   相关客户端，权限由容器自己的 `AccessMode` 决定。
 * - **开箱表现**通过 `BP_OnChestOpened` / `BP_OnChestClosed` 交给蓝图
 *   （动画、音效、粒子都行），C++ 只负责在正确的端、正确的时机调用。
 * - **窗口**由宝箱蓝图类默认值里的 `ChestWindowClass` 指定；C++ 创建窗口并
 *   注入容器，蓝图 WBP 负责外观。
 *
 * ## 打开 / 关闭流程
 *
 * 打开（客户端输入 -> 服务器）：
 *   1. 蓝图 / 输入调用 `OpenChest`（任意端都可以）；它找到玩家角色上的
 *      `UContainerInteractionComponent` 并转发到 `Server_RequestOpenContainer`
 *      （世界 Actor 没有 owning connection，不能直接对宝箱发 Server RPC）；
 *   2. 服务器校验玩家、权限、距离；
 *   3. 服务器调用 `OpenChestOnServer`：校验容器权限与 `ChestWindowClass`，把
 *      `bIsOpened` 置 true；
 *   4. `OnRep_bIsOpened` 在各端调用 `BP_OnChestOpened`；
 *   5. 交互组件把容器写进玩家的 `ActiveContainer`，拥有者客户端据此创建
 *      `ChestWindowClass` 窗口。
 *
 * 关闭（窗口 -> 服务器）：
 *   1. 窗口调用玩家的 `Server_RequestCloseContainer`；
 *   2. 服务器调用 `CloseChest`，从 `ActiveUsers` 移除该玩家；
 *   3. 最后一名打开者离开时 `bIsOpened=false`，各端 `OnRep_bIsOpened`
 *      调用 `BP_OnChestClosed`（关箱动画）；
 *   4. 服务器清空 `ActiveContainer`，拥有者客户端销毁窗口。
 *
 * 用 `ActiveUsers` 而不是单个 bool 判断「还有没有人开着」：多个玩家同时查看
 * 同一个公共宝箱时，第一个人关闭不应让第二个人的箱子视觉上关掉。
 *
 * ## 掉落
 *
 * `BeginPlay`（仅服务器）按 `ChestLevel` 从物品 DataTable 里筛选稀有度合适的
 * 物品，随机挑 `LootCountMin ~ LootCountMax` 堆放进容器；每堆的数量在
 * `LootStackMin ~ LootStackMax` 内随机，并按物品自身的 `IsStackable` /
 * `MaxStackSize` 夹紧（不可堆叠物品恒为 1）。等级、堆数与每堆数量的全局
 * 默认值可以用 `UChestSettings`（ini）覆盖；每个宝箱也可以在类默认值里单独改。
 */
UCLASS()
class WILDFORGE_API AChestActor : public AActor {
  GENERATED_BODY()

public:
  AChestActor();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /** 宝箱等级（复制到各端，供 UI / 表现读取）。 */
  UFUNCTION(BlueprintPure, Category = "Chest", meta = (BlueprintThreadSafe))
  EChestLevel GetChestLevel() const { return ChestLevel; }

  /**
   * 服务器修改宝箱等级（升级 / 降级）。
   *
   * 只改变后续表现与 UI，**不会**重新生成已经放进容器的战利品；需要重抽时
   * 由服务器清空容器后重新调用掉落生成流程。
   */
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Chest")
  void SetChestLevel(EChestLevel InChestLevel);

  /** 宝箱的存储组件（世界容器，内容复制给相关客户端）。 */
  UFUNCTION(BlueprintPure, Category = "Chest", meta = (BlueprintThreadSafe))
  UStorageContainer *GetContainer() const { return Container.Get(); }

  /** 宝箱窗口类（由宝箱蓝图类默认值指定，可为空 = 没有窗口）。 */
  UFUNCTION(BlueprintPure, Category = "Chest|UI", meta = (BlueprintThreadSafe))
  TSubclassOf<UChestWindowWidget> GetChestWindowClass() const {
    return ChestWindowClass;
  }

  /**
   * 打开宝箱的**统一入口**（蓝图 / 输入调这个，任意端都可以）。
   *
   * 它不直接改状态，而是找到 `Player` 控制的 Pawn 上的
   * `UContainerInteractionComponent`，转发 `Server_RequestOpenContainer`：
   *   - 服务器上 Server RPC 就地同步执行，随后 `SetActiveContainer` 创建窗口；
   *   - 客户端上 RPC 发到服务器，`ActiveContainer` 复制回来后再创建窗口。
   * 因此蓝图不需要自己判断权威端，也不会再出现「客户端直接调权威函数、
   * 被引擎静默丢弃」的失效。
   *
   * 返回值只表示**请求是否已发出**（客户端拿不到服务器的即时结论）；
   * 真正的权限 / 距离 / 窗口类校验在 `OpenChestOnServer` 里。
   */
  UFUNCTION(BlueprintCallable, Category = "Chest")
  bool OpenChest(APlayerController *Player);

  /**
   * 服务器专用实现：校验容器权限与 `ChestWindowClass`，把 `bIsOpened` 置
   * true（触发 `BP_OnChestOpened`）并维护 `ActiveUsers`。
   *
   * **只应由 `UContainerInteractionComponent::Server_RequestOpenContainer`
   * 在服务器上调用**（窗口显示依赖它在同一个调用链里写入客户端可用状态）。
   * 蓝图请调 `OpenChest`，不要直接调这个——客户端没有 owning connection，
   * 直接对宝箱发请求会被引擎丢弃（bug-093）。
   *
   * 只做权限与配置校验，**不校验距离**（距离属于玩家交互组件）。
   */
  bool OpenChestOnServer(APlayerController *Player);

  /**
   * 服务器：记录玩家离开宝箱；最后一名打开者离开时触关箱表现
   * （`BP_OnChestClosed`）。
   *
   * 关闭**不做权限检查**：权限被撤销、玩家死亡 / 断线时也必须能把箱子关掉，
   * 否则视觉状态会永久卡在打开。`Player == nullptr` 表示强制关闭所有打开者
   * （用于断线清理）。
   */
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Chest")
  bool CloseChest(APlayerController *Player = nullptr);

protected:
  virtual void BeginPlay() override;

  /** 复制状态变化：打开 / 关闭各调用一次蓝图表现事件。 */
  UFUNCTION()
  void OnRep_bIsOpened();

  /** 等级复制到客户端 / 服务器本地修改后，通知蓝图刷新外观。 */
  UFUNCTION()
  void OnRep_ChestLevel();

  /**
   * 打开动画 / 音效 / 粒子等表现（蓝图实现）。
   *
   * 在服务器与所有收到该 Actor 的客户端上都会触发；专用服务器上没有画面，
   * 但蓝图事件仍会执行（不要在这里放纯客户端 UI）。
   */
  UFUNCTION(BlueprintImplementableEvent, Category = "Chest|Animation")
  void BP_OnChestOpened();

  /** 关闭动画 / 音效 / 粒子等表现（蓝图实现）。 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Chest|Animation")
  void BP_OnChestClosed();

  /** 等级变化时刷新颜色 / 图标（蓝图实现）。 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Chest|Animation")
  void BP_OnChestLevelChanged(EChestLevel NewLevel);

  /** 宝箱的存储容器。必须在 BeginPlay 前存在（本类构造函数已创建）。 */
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chest")
  TObjectPtr<UStorageContainer> Container;

  /** 宝箱等级：决定掉落稀有度池与表现。 */
  UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_ChestLevel,
            BlueprintReadOnly, Category = "Chest")
  EChestLevel ChestLevel = EChestLevel::Common;

  /**
   * 宝箱窗口 UI 类（在宝箱蓝图类默认值里指定一个继承
   * `UChestWindowWidget` 的 WBP）。
   *
   * 为空时 `OpenChestOnServer` 会拒绝打开：否则玩家会进入「有活动容器但
   * 没有窗口」的卡死状态。
   */
  UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Chest|UI",
            meta = (DisplayName = "宝箱窗口 UI 类"))
  TSubclassOf<UChestWindowWidget> ChestWindowClass;

  /**
   * 容器权限模式。宝箱默认 `Public`（谁都能开）；
   * 私有储物箱请在类默认值里改成 `OwnerOnly` / `Whitelist` 并在服务器设置
   * `AccessOwner` / `AllowedPlayers`。
   *
   * 服务器在 `BeginPlay` 把它写进 `Container`（会覆盖组件上的同名设置）。
   */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Access")
  EContainerAccessMode AccessMode = EContainerAccessMode::Public;

  /** BeginPlay（服务器）是否自动按等级生成战利品。 */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Loot")
  bool bGenerateLootOnBeginPlay = true;

  /** 容器格数（类默认值；可被 `UChestSettings` 的 ini 覆盖）。 */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Loot",
            meta = (ClampMin = "1", UIMin = "1"))
  int32 Capacity = 12;

  /** 随机生成战利品的最少件数（类默认值；可被 ini 覆盖）。 */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Loot",
            meta = (ClampMin = "0", UIMin = "0"))
  int32 LootCountMin = 2;

  /** 随机生成战利品的最多件数（小于下限时会被抬到下限）。 */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Loot",
            meta = (ClampMin = "0", UIMin = "0"))
  int32 LootCountMax = 4;

  /** 每堆战利品随机数量的下限（按物品 MaxStackSize 夹紧；不可堆叠物品恒为 1）。 */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Loot",
            meta = (ClampMin = "1", UIMin = "1"))
  int32 LootStackMin = 1;

  /** 每堆战利品随机数量的上限（小于下限时会被抬到下限，且不超过物品 MaxStackSize）。 */
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Chest|Loot",
            meta = (ClampMin = "1", UIMin = "1"))
  int32 LootStackMax = 3;

  /** 是否处于打开表现状态（复制；变化时触发蓝图开 / 关事件）。 */
  UPROPERTY(ReplicatedUsing = OnRep_bIsOpened, BlueprintReadOnly,
            Category = "Chest|State")
  bool bIsOpened = false;

private:
  /** 权威端：读取 `UChestSettings` 的 ini 覆盖并夹紧。 */
  void ApplyGameplaySettingsOverrides();

  /** 权威端：按等级筛选 DataTable 物品并随机放入容器。 */
  void GenerateLoot();

  /** 服务器：设置打开状态并在本地补一次 OnRep（服务器不会收到自己的 RepNotify）。 */
  void SetOpenedState(bool bNewOpened);

  /** 清理 `ActiveUsers` 里已经失效的 PlayerController。 */
  void CleanupActiveUsers();

  /**
   * 服务器：当前打开着这个宝箱的玩家。
   *
   * 用弱引用是因为 PlayerController 可能比箱子先消失（断线 / 换关卡）；
   * 集合为空才真正播放关箱表现。不复制（客户端只关心 `bIsOpened`）。
   */
  TSet<TWeakObjectPtr<APlayerController>> ActiveUsers;
};
