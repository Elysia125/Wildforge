#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "ItemSystem/Components/ItemContainer.h"

#include "ContainerInteractionComponent.generated.h"

class APlayerController;
class UChestWindowWidget;

/** 玩家当前打开的容器变化（服务器写入后广播；拥有者客户端收到复制后也会广播）。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnActiveContainerChanged,
                                            UItemContainer *, ActiveContainer);

/**
 * 玩家侧的容器交互通道（挂在 `APlayerCharacter` 上，每个玩家一份）。
 *
 * ## 为什么需要它
 *
 * `UItemContainer` 自己的 `Server_*` RPC 依赖**拥有连接**：只有「该 Actor 由该客户端
 * 的 PlayerController 拥有」时，引擎才会把 Server RPC 交给服务器
 * （`UNetDriver::ShouldCallRemoteFunction` 的 `bNetOwner`，NetDriver.cpp:8242-8245）。
 * 世界宝箱是服务器生成的关卡 actor，客户端往上发的 Server RPC 会被**静默丢弃**
 * （见 bug-093）。所以跨容器 / 宝箱交互的 RPC 必须挂在**玩家自己的角色**上，
 * 由服务器在角色上校验「玩家 → 容器」的权限与距离。
 *
 * ## 四个入口
 *
 * - `Server_MoveItemBetweenContainers`：把物品从任意一个容器（背包 / 快捷栏 / 宝箱）
 *   移到另一个容器。同容器拖拽也走它，和跨容器拖拽统一一条服务器校验路径。
 * - `Server_TransferAllFromContainer`：把源容器里的物品全部转入目标容器
 *   （宝箱窗口的「全部转移」按钮）。目标优先合并同 ItemID 的未满堆，装不下的
 *   部分留在源容器。
 * - `Server_RequestOpenContainer` / `Server_RequestCloseContainer`：记录玩家当前
 *   打开的容器（`ActiveContainer`，`COND_OwnerOnly` 复制给拥有者），宝箱 UI 订阅
 *   `OnActiveContainerChanged` 后按它显示内容。
 *
 * ## 宝箱窗口
 *
 * 打开的是 `AChestActor` 的容器时，本组件负责创建 / 销毁宝箱窗口：
 * 服务器把容器写入 `ActiveContainer` 后，拥有者客户端在 `OnRep_ActiveContainer`
 * 里按宝箱 Actor 的 `ChestWindowClass` 创建 `UChestWindowWidget`；关闭时服务器
 * 清空 `ActiveContainer`，同一处销毁窗口。窗口本身不直接对宝箱发 RPC
 * （世界 Actor 没有 owning connection，见 bug-093），而是回调本组件的
 * `Server_RequestCloseContainer`，由服务器统一调用 `AChestActor::CloseChest`
 * 播放关箱表现。
 *
 * ## 信任边界
 *
 * 客户端只传**容器指针 + 槽位索引**，绝不传 `FItemInformation`；物品数据由服务器
 * 从源容器里读。服务器在任何修改前都重新校验：
 *   1. RPC 的调用者确实是本组件的拥有者（`PC->GetPawn() == GetOwner()`）；
 *   2. 玩家对源 / 目标容器都有 `HasAccess`（权限模式 + 白名单）；
 *   3. 两个容器都在交互距离内（玩家自己的容器不受距离限制）。
 * 客户端的 `CanInteractWith` 只用于 UI 预检（高亮 / 是否允许落点），不能当作
 * 服务器结论。
 *
 * ## 数值配置
 *
 * `MaxInteractionDistance` 由服务器在 `BeginPlay` 读
 * `UContainerInteractionSettings`（ini）后写入，并随出生束复制到各端
 * （`COND_InitialOnly`）。客户端本地那份 ini 是玩家可改的文件，不能拿它驱动玩法。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
       BlueprintType, Blueprintable)
class WILDFORGE_API UContainerInteractionComponent : public UActorComponent {
  GENERATED_BODY()

public:
  UContainerInteractionComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  /**
   * 玩家当前打开的容器（服务器写入；只有拥有者客户端收得到）。
   * 容器在打开期间被销毁时返回空：`TObjectPtr::Get()` 不会替我们过滤
   * pending-kill，UI 拿到已销毁容器的指针会立即踩断言。
   */
  UFUNCTION(BlueprintPure, Category = "Container|Interaction",
            meta = (BlueprintThreadSafe))
  UItemContainer *GetActiveContainer() const {
    UItemContainer *Container = ActiveContainer.Get();
    return IsValid(Container) ? Container : nullptr;
  }

  /**
   * 本地预检：玩家是否对 `Container` 有权限，且容器在交互距离内。
   * 只用于 UI（能否高亮 / 能否落点）；服务器会在 RPC 里重新校验。
   */
  UFUNCTION(BlueprintPure, Category = "Container|Interaction",
            meta = (BlueprintThreadSafe))
  bool CanInteractWith(UItemContainer *Container) const;

  UFUNCTION(BlueprintPure, Category = "Container|Interaction",
            meta = (BlueprintThreadSafe))
  float GetMaxInteractionDistance() const { return MaxInteractionDistance; }

  // ===== 客户端 -> 服务器：容器交互请求 =====

  /** 请求打开一个容器（宝箱 UI 的入口）。服务器校验权限与距离后写入 ActiveContainer。 */
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Container|Interaction|RPC")
  void Server_RequestOpenContainer(UItemContainer *Container);

  /** 请求关闭当前容器。`Container` 只是用于日志核对，可传当前容器或空。 */
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Container|Interaction|RPC")
  void Server_RequestCloseContainer(UItemContainer *Container);

  /**
   * 把物品从 FromContainer[FromSlot] 转移到 ToContainer[ToSlot]。
   * 两个容器可以相同（同容器拖拽），也可以不同（背包 <-> 快捷栏 / 宝箱）。
   * 服务器负责权限、距离与合并 / 交换规则；客户端不传物品数据。
   */
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Container|Interaction|RPC")
  void Server_MoveItemBetweenContainers(UItemContainer *FromContainer,
                                        int32 FromSlot,
                                        UItemContainer *ToContainer,
                                        int32 ToSlot);

  /**
   * 请求把 `FromContainer` 里的物品**全部**转移到 `ToContainer`
   * （宝箱窗口的「全部转移」按钮）。
   *
   * 服务器校验权限与距离后调用 `UItemContainer::TransferAllToContainer`：
   * 目标优先合并同 ItemID 的未满堆，剩余数量再放入空槽；装不下的部分留在
   * 源容器。客户端只传容器指针，不传任何物品数据。
   */
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Container|Interaction|RPC")
  void Server_TransferAllFromContainer(UItemContainer *FromContainer,
                                       UItemContainer *ToContainer);

  /** 当前打开的容器变化时广播（服务器写入后本地广播；拥有者客户端 OnRep 后广播）。 */
  UPROPERTY(BlueprintAssignable, Category = "Container|Interaction")
  FOnActiveContainerChanged OnActiveContainerChanged;

protected:
  virtual void BeginPlay() override;

  /**
   * 组件销毁（角色死亡 / 断线 / 换关卡）时把当前宝箱关掉，避免箱子视觉上
   * 永久停在打开状态。权威端调用 `AChestActor::CloseChest`；所有端销毁窗口。
   */
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  /**
   * 当前打开的容器。只复制给拥有者：只有本玩家的 UI 需要知道「我正开着哪个箱子」。
   * `COND_OwnerOnly` + `RepNotify`：服务器写入后手动调一次 OnRep 广播给本地 UI，
   * 拥有者客户端靠引擎的 OnRep 广播。这样监听服务器不会因为「本地也收到一次 Client RPC」
   * 而重复广播。
   */
  UPROPERTY(ReplicatedUsing = OnRep_ActiveContainer,
            BlueprintGetter = GetActiveContainer, Category = "Container|Interaction")
  TObjectPtr<UItemContainer> ActiveContainer = nullptr;

  /**
   * 最大交互距离（厘米）。服务器读 ini 后写入，随出生束复制（`COND_InitialOnly`），
   * 客户端只认这份复制值。
   */
  UPROPERTY(Replicated, BlueprintReadOnly, Category = "Container|Interaction",
            meta = (ClampMin = "0.0"))
  float MaxInteractionDistance = 250.f;

  UFUNCTION()
  void OnRep_ActiveContainer();

private:
  /**
   * 权威端：把 `UContainerInteractionSettings` 的 ini 覆盖写进
   * `MaxInteractionDistance`。客户端直接返回（只认复制值）。
   */
  void ApplyGameplaySettingsOverrides();

  /** 本组件宿主 Pawn 的 PlayerController（服务器与拥有者客户端都拿得到）。 */
  APlayerController *GetOwningPlayerController() const;

  /**
   * 容器是否在交互距离内。玩家自己的容器（同一 Actor）永远为 true；
   * 世界容器按 3D 距离与 `MaxInteractionDistance` 比较。
   */
  bool HasInteractionRange(UItemContainer *Container,
                           APlayerController *Player) const;

  /**
   * 两个转移 RPC 共用的服务器校验：调用者身份、源 / 目标容器权限、交互距离。
   * 只有全部通过才允许修改容器。`ActionName` 只用于日志。
   */
  bool ValidateTransferRequest(UItemContainer *FromContainer,
                               UItemContainer *ToContainer,
                               const TCHAR *ActionName) const;

  /** 服务器：写入 ActiveContainer 并广播（服务器不会自动收到 OnRep）。 */
  void SetActiveContainer(UItemContainer *Container);

  /**
   * 根据当前活动容器创建 / 销毁宝箱窗口。
   *
   * 只在本地控制器上创建 UMG；专用服务器与模拟代理直接走销毁分支。
   * 非宝箱容器（玩家背包 / 快捷栏）不创建窗口。
   *
   * 窗口创建后按 WBP 的 `WindowSize` 居中定位，并切换到 `FInputModeUIOnly`
   * （开箱期间不接收游戏输入）。
   */
  void UpdateChestWindow(UItemContainer *Container);

  /**
   * 销毁当前宝箱窗口并恢复输入模式（若本组件设置过）。
   *
   * 背包仍打开时恢复背包用的 `FInputModeGameAndUI`（保留鼠标与游戏输入），
   * 否则恢复 `FInputModeGameOnly`。
   */
  void DestroyChestWindow();

  /** 本地背包 UI 是否正开着（关闭宝箱窗口时不要抢走它的鼠标 / 输入模式）。 */
  bool IsInventoryOpenOnOwningPlayer() const;

  /** 当前本地宝箱窗口（仅拥有者客户端 / 监听服务器的主机拥有）。 */
  UPROPERTY(Transient)
  TObjectPtr<UChestWindowWidget> ChestWindow = nullptr;

  /** 输入模式是否由本组件为宝箱窗口设置过（避免误恢复其它 UI 的状态）。 */
  bool bAppliedChestInputMode = false;
};
