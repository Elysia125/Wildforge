#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"

#include "ChestWindowWidget.generated.h"

class AChestActor;
class UButton;
class UContainerInteractionComponent;
class UItemContainer;
class UItemContainerGrid;

/**
 * 宝箱窗口的 C++ 基类（必须由蓝图子类指定具体外观）。
 *
 * ## 为什么需要基类
 *
 * 宝箱窗口的 UI 类由宝箱 Actor 的 `ChestWindowClass` 指定（在宝箱蓝图类默认值里
 * 选一个 WBP）。C++ 侧需要把「这是哪个箱子、哪个容器」交给它，而任意
 * `UUserWidget` 没有这个入口，所以这里提供一个稳定的基类：
 *
 *   - `UChestWindowWidget`（本类，Abstract）负责数据注入、关闭请求与「全部转移」；
 *   - 策划在 WBP 里继承它，放两个 `UItemContainerGrid`：`ChestContainerGrid`
 *     （宝箱物品）与 `UserInventoryGrid`（玩家背包），窗口打开时自动绑定容器；
 *   - 两个可选按钮：`CloseButton`（关闭）与 `TransferAllButton`（把宝箱物品
 *     全部转入玩家背包，装不下的留在宝箱）；
 *   - `OnChestWindowInitialized` 给蓝图做标题、等级颜色、开窗动画等收尾。
 *
 * ## 关闭流程（为什么窗口不直接调用宝箱）
 *
 * 世界宝箱没有客户端 owning connection，客户端直接对宝箱 Actor 发 Server RPC
 * 会被引擎**静默丢弃**（bug-093）。所以关闭窗口走玩家身上的
 * `UContainerInteractionComponent::Server_RequestCloseContainer`：
 *
 *   窗口关闭按钮 / `NativeDestruct`
 *     -> 玩家交互组件 Server RPC
 *     -> 服务器 `AChestActor::CloseChest`（设置复制状态 `bIsOpened=false`）
 *     -> 各端 `OnRep_bIsOpened` -> 蓝图 `BP_OnChestClosed`（播放关箱动画）
 *     -> 服务器清空 `ActiveContainer` -> 拥有者客户端销毁本窗口。
 *
 * 这样「窗口关闭」和「宝箱表现」由服务器统一编排，不依赖客户端自觉。
 *
 * ## 窗口尺寸与输入
 *
 * `UUserWidget::AddToViewport` 默认给控件一个铺满全屏的视口槽
 * （`FGameViewportWidgetSlot` 的 Anchors 是 (0,0)-(1,1)），所以 WBP 根控件
 * 会被拉满屏幕。C++ 在挂到视口后调用 `ApplyWindowLayout()`，按 `WindowSize`
 * 固定尺寸并居中；`WindowSize` 可以在 WBP 的 Class Defaults 里调整。
 *
 * 窗口打开期间使用 `FInputModeUIOnly`：游戏输入（移动 / 攻击 / 交互）全部
 * 暂停，只保留 UI 点击；关闭窗口时由 `UContainerInteractionComponent`
 * 恢复输入模式。
 */
UCLASS(Abstract)
class WILDFORGE_API UChestWindowWidget : public UUserWidget {
  GENERATED_BODY()

public:
  /**
   * 注入宝箱与容器并初始化窗口。由 `UContainerInteractionComponent` 在创建
   * 窗口后调用；蓝图一般不需要手动调。
   */
  UFUNCTION(BlueprintCallable, Category = "Chest|UI")
  void InitializeChestWindow(AChestActor *InChestActor,
                             UItemContainer *InContainer);

  /**
   * 请求关闭当前宝箱窗口（关闭按钮绑定到这里）。
   *
   * 只发请求，不立即销毁控件：服务器确认后会把 `ActiveContainer` 清空，
   * 拥有者客户端收到复制后统一销毁窗口。这样服务器的权威状态和 UI 始终一致。
   */
  UFUNCTION(BlueprintCallable, Category = "Chest|UI")
  void RequestCloseChest();

  /**
   * 请求把宝箱里的物品全部转移到玩家背包（转移按钮绑定到这里）。
   *
   * 实际转移由服务器执行：目标背包优先合并到同 ItemID 的未满堆，剩余数量
   * 再放入空槽；背包装不下的部分留在宝箱里。客户端只发请求，不传物品数据。
   */
  UFUNCTION(BlueprintCallable, Category = "Chest|UI")
  void RequestTransferAllFromChest();

  UFUNCTION(BlueprintPure, Category = "Chest|UI")
  AChestActor *GetChestActor() const;

  UFUNCTION(BlueprintPure, Category = "Chest|UI")
  UItemContainer *GetContainer() const;

  /**
   * 按 `WindowSize` 把窗口以视口中心定位（在 `AddToViewport` 之后调用）。
   *
   * 蓝图在运行时改了 `WindowSize` 后也可以再调一次立即生效。
   */
  UFUNCTION(BlueprintCallable, Category = "Chest|UI")
  void ApplyWindowLayout();

protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** 关闭按钮：服务器确认后统一销毁窗口（不在这里直接 RemoveFromParent）。 */
  UFUNCTION()
  void OnCloseButtonClicked();

  /** 转移全部按钮：向服务器请求把宝箱物品转入玩家背包。 */
  UFUNCTION()
  void OnTransferAllButtonClicked();

  /**
   * 关闭按钮的点击表现（蓝图实现）。
   *
   * C++ 在发出关闭请求**之前**调用，蓝图里可以播放按钮按压 / 窗口收起的
   * Widget 动画。默认实现为空，不实现也不影响关闭功能。
   */
  UFUNCTION(BlueprintImplementableEvent, Category = "Chest|UI")
  void BP_OnCloseButtonClicked();

  /**
   * 转移全部按钮的点击表现（蓝图实现）。
   *
   * C++ 在发出转移请求**之前**调用，蓝图里可以播放按钮按压动画。
   * 默认实现为空，不实现也不影响转移功能。
   */
  UFUNCTION(BlueprintImplementableEvent, Category = "Chest|UI")
  void BP_OnTransferAllButtonClicked();

  /**
   * 窗口数据注入完成（容器已绑定、物品网格已刷新）。
   *
   * 蓝图在这里设置标题、等级配色、播放开窗动画等。注意本事件在窗口
   * `AddToViewport` 之前触发，控件树已经创建，但控件动画需要等构造完成后
   * 才能可靠播放；需要动画时可在蓝图的 Construct / 动画事件里处理。
   */
  UFUNCTION(BlueprintImplementableEvent, Category = "Chest|UI")
  void OnChestWindowInitialized();

  /**
   * 窗口在视口中的固定尺寸（Slate 单位，不含 DPI 缩放）。
   *
   * 在 WBP 的 Class Defaults 里调整；C++ 在 `AddToViewport` 之后按它居中。
   * 只改变整个窗口的大小，窗口内部的布局仍由 WBP 自己决定。
   */
  UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Chest|UI",
            meta = (DisplayName = "窗口尺寸"))
  FVector2D WindowSize = FVector2D(900.0, 600.0);

  /**
   * 宝箱物品网格（可选绑定，UMG 里的控件名必须是 `ChestContainerGrid`）。
   *
   * 绑定存在时，`InitializeChestWindow` 会调用它的 `InitializeGrid(宝箱容器)`，
   * 自动生成宝箱槽位；不绑定则由蓝图自行显示容器内容。
   */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UItemContainerGrid> ChestContainerGrid;

  /**
   * 玩家背包网格（可选绑定，UMG 里的控件名必须是 `UserInventoryGrid`）。
   *
   * 绑定存在时，`InitializeChestWindow` 会从拥有者 Pawn 上找到 `UPlayerInventory`
   * 并调用 `InitializeGrid(背包容器)`；找不到时记 WARNING，网格保持空白。
   */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UItemContainerGrid> UserInventoryGrid;

  /** 关闭按钮（可选绑定，UMG 里的控件名必须是 `CloseButton`）。 */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UButton> CloseButton;

  /** 转移全部按钮（可选绑定，UMG 里的控件名必须是 `TransferAllButton`）。 */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UButton> TransferAllButton;

private:
  // 弱引用：窗口可能在关卡切换 / 宝箱被销毁后才析构，裸指针会踩已回收对象
  // （与 ItemContainerGrid / InventorySlotWidget 的 bug-064 是同一类问题）。
  UPROPERTY(Transient)
  TWeakObjectPtr<AChestActor> ChestActor;

  UPROPERTY(Transient)
  TWeakObjectPtr<UItemContainer> Container;

  /**
   * 解析「当前窗口对应的玩家交互组件」：窗口只对活动容器发请求，避免
   * 服务器已经关闭 / 切换到别的容器后还发冗余 RPC。
   * 条件不满足时记日志并返回 nullptr。
   */
  UContainerInteractionComponent *
  ResolveInteractionForActiveContainer(UItemContainer *LiveContainer) const;
};
