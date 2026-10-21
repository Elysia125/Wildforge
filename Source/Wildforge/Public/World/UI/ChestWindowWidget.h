#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"

#include "ChestWindowWidget.generated.h"

class AChestActor;
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
 *   - `UChestWindowWidget`（本类，Abstract）负责数据注入与关闭请求；
 *   - 策划在 WBP 里继承它，放一个名为 `ItemContainerGrid` 的 `UItemContainerGrid`
 *     （可选绑定），窗口打开时自动绑定宝箱容器并显示物品；
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

  UFUNCTION(BlueprintPure, Category = "Chest|UI")
  AChestActor *GetChestActor() const;

  UFUNCTION(BlueprintPure, Category = "Chest|UI")
  UItemContainer *GetContainer() const;

protected:
  virtual void NativeDestruct() override;

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
   * 物品网格（可选绑定，UMG 里的名字必须是 `ItemContainerGrid`）。
   *
   * 绑定存在时，`InitializeChestWindow` 会调用它的 `InitializeGrid(Container)`，
   * 自动生成宝箱槽位；不绑定则由蓝图自行显示容器内容。
   */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UItemContainerGrid> ItemContainerGrid;

private:
  // 弱引用：窗口可能在关卡切换 / 宝箱被销毁后才析构，裸指针会踩已回收对象
  // （与 ItemContainerGrid / InventorySlotWidget 的 bug-064 是同一类问题）。
  UPROPERTY(Transient)
  TWeakObjectPtr<AChestActor> ChestActor;

  UPROPERTY(Transient)
  TWeakObjectPtr<UItemContainer> Container;
};
