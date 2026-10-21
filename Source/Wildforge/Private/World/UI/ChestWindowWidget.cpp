#include "World/UI/ChestWindowWidget.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ItemSystem/Components/ContainerInteractionComponent.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/UI/ItemContainerGrid.h"
#include "Utils/WildforgeLog.h"
#include "World/ChestActor.h"

void UChestWindowWidget::InitializeChestWindow(AChestActor *InChestActor,
                                               UItemContainer *InContainer) {
  ChestActor = InChestActor;
  Container = InContainer;

  if (!InContainer) {
    WFLOG_ERROR("[宝箱UI] %s 初始化失败：容器为空。",
                *GetClass()->GetName());
  } else if (ItemContainerGrid) {
    // 网格自己会订阅容器的 OnContainerChanged；这里只做一次全量绑定。
    ItemContainerGrid->InitializeGrid(InContainer);
  } else {
    WFLOG_WARNING("[宝箱UI] %s 没有绑定名为 ItemContainerGrid 的控件；"
                  "宝箱物品不会自动显示。请在 WBP 里放一个 UItemContainerGrid 并把"
                  "控件名设为 ItemContainerGrid，或在 OnChestWindowInitialized 里"
                  "自行绑定 GetContainer()。",
                  *GetClass()->GetName());
  }

  WFLOG_INFO("[宝箱UI] %s 初始化：宝箱=%s（等级=%d），容器=%s（容量 %d），"
             "拥有者=%s。",
             *GetClass()->GetName(), *GetNameSafe(InChestActor),
             InChestActor ? static_cast<int32>(InChestActor->GetChestLevel()) : -1,
             *GetNameSafe(InContainer),
             InContainer ? InContainer->GetCapacity() : 0,
             *GetNameSafe(GetOwningPlayer()));

  // 蓝图在这里做标题、等级配色、开窗动画等收尾。
  OnChestWindowInitialized();
}

void UChestWindowWidget::RequestCloseChest() {
  UItemContainer *LiveContainer = Container.Get();
  if (!LiveContainer) {
    // 容器已经被销毁：没有可关的箱子，也无需再发 RPC。
    return;
  }

  // 关卡切换 / 退出游戏时组件正在析构，此时发 RPC 只会在日志里留下
  // 「pending kill」噪音；直接放弃关闭请求。
  UWorld *World = GetWorld();
  if (!World || World->bIsTearingDown) {
    return;
  }

  APlayerController *PC = GetOwningPlayer();
  if (!PC) {
    WFLOG_WARNING("[宝箱UI] %s 请求关闭宝箱，但拿不到 OwningPlayer。",
                  *GetClass()->GetName());
    return;
  }

  APawn *Pawn = PC->GetPawn();
  UContainerInteractionComponent *Interaction =
      Pawn ? Pawn->FindComponentByClass<UContainerInteractionComponent>()
           : nullptr;
  if (!Interaction) {
    WFLOG_WARNING("[宝箱UI] 玩家 %s 的 Pawn %s 上没有 "
                  "UContainerInteractionComponent，无法通知服务器关闭宝箱。",
                  *GetNameSafe(PC), *GetNameSafe(Pawn));
    return;
  }

  // 服务器可能已经先一步关闭（例如最后一名打开者离开，或箱子被销毁）：
  // ActiveContainer 已经不是这个容器时，不再重复发请求。
  if (Interaction->GetActiveContainer() != LiveContainer) {
    WFLOG_INFO("[宝箱UI] 当前活动容器已不是 %s，忽略关闭请求"
               "（窗口正在被服务器销毁）。",
               *GetNameSafe(LiveContainer));
    return;
  }

  WFLOG_INFO("[宝箱UI] 玩家 %s 请求关闭宝箱窗口（容器=%s）。",
             *GetNameSafe(PC), *GetNameSafe(LiveContainer));

  // 宝箱 Actor 没有 owning connection，不能直接对它发 Server RPC；
  // 必须走玩家身上的交互组件（见类注释与 bug-093）。
  Interaction->Server_RequestCloseContainer(LiveContainer);
}

void UChestWindowWidget::NativeDestruct() {
  // 覆盖关闭按钮之外的销毁路径（蓝图 RemoveFromParent、父级被销毁等）：
  // 如果服务器还没关闭，这里补一次请求。已在服务器关闭流程里被销毁时，
  // RequestCloseChest 会因为 ActiveContainer 已空而直接返回，不会重复发 RPC。
  RequestCloseChest();

  Super::NativeDestruct();
}
