#include "World/UI/ChestWindowWidget.h"

#include "Components/Button.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ItemSystem/Components/ContainerInteractionComponent.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Components/PlayerInventory.h"
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
  } else if (ChestContainerGrid) {
    // 网格自己会订阅容器的 OnContainerChanged；这里只做一次全量绑定。
    ChestContainerGrid->InitializeGrid(InContainer);
  } else {
    WFLOG_WARNING("[宝箱UI] %s 没有绑定名为 ChestContainerGrid 的控件；"
                  "宝箱物品不会自动显示。请在 WBP 里放一个 UItemContainerGrid 并把"
                  "控件名设为 ChestContainerGrid，或在 OnChestWindowInitialized 里"
                  "自行绑定 GetContainer()。",
                  *GetClass()->GetName());
  }

  // 玩家背包网格：背包是拥有者 Pawn 上的 UPlayerInventory 组件（不在
  // PlayerController 上）。客户端的背包内容靠 COND_OwnerOnly 复制，窗口创建的
  // 时候通常已经到位；即使容量还是 0，InitializeGrid 也会订阅 OnContainerChanged，
  // 复制到达后由 HandleContainerChanged 补建槽位——因此不要以「容量为 0」跳过绑定。
  APawn *OwningPawn = GetOwningPlayerPawn();
  UPlayerInventory *PlayerInventory =
      OwningPawn ? OwningPawn->FindComponentByClass<UPlayerInventory>()
                 : nullptr;
  if (UserInventoryGrid) {
    if (PlayerInventory) {
      UserInventoryGrid->InitializeGrid(PlayerInventory);
    } else {
      WFLOG_WARNING("[宝箱UI] %s 绑定了 UserInventoryGrid，但拥有者 Pawn %s 上"
                    "找不到 UPlayerInventory；玩家背包格不会显示。",
                    *GetClass()->GetName(), *GetNameSafe(OwningPawn));
    }
  } else {
    WFLOG_WARNING("[宝箱UI] %s 没有绑定名为 UserInventoryGrid 的控件；"
                  "玩家背包不会显示。请在 WBP 里放一个 UItemContainerGrid 并把"
                  "控件名设为 UserInventoryGrid。",
                  *GetClass()->GetName());
  }

  WFLOG_INFO("[宝箱UI] %s 初始化：宝箱=%s（等级=%d），容器=%s（容量 %d），"
             "拥有者=%s，背包容器=%s（容量 %d）。",
             *GetClass()->GetName(), *GetNameSafe(InChestActor),
             InChestActor ? static_cast<int32>(InChestActor->GetChestLevel()) : -1,
             *GetNameSafe(InContainer),
             InContainer ? InContainer->GetCapacity() : 0,
             *GetNameSafe(GetOwningPlayer()), *GetNameSafe(PlayerInventory),
             PlayerInventory ? PlayerInventory->GetCapacity() : 0);

  // 蓝图在这里做标题、等级配色、开窗动画等收尾。
  OnChestWindowInitialized();
}

void UChestWindowWidget::NativeConstruct() {
  Super::NativeConstruct();

  // 动态委托的 AddDynamic 是 AddUnique 语义：NativeConstruct 重跑不会重复绑定。
  if (CloseButton) {
    CloseButton->OnClicked.AddDynamic(this,
                                      &UChestWindowWidget::OnCloseButtonClicked);
  }
  if (TransferAllButton) {
    TransferAllButton->OnClicked.AddDynamic(
        this, &UChestWindowWidget::OnTransferAllButtonClicked);
  }

  if (!CloseButton || !TransferAllButton) {
    WFLOG_WARNING("[宝箱UI] %s 缺少按钮绑定（CloseButton=%s，"
                  "TransferAllButton=%s）。请在 WBP 里放两个 UButton，"
                  "控件名分别设为 CloseButton / TransferAllButton。",
                  *GetClass()->GetName(),
                  CloseButton ? TEXT("已绑定") : TEXT("缺失"),
                  TransferAllButton ? TEXT("已绑定") : TEXT("缺失"));
  }
}

void UChestWindowWidget::OnCloseButtonClicked() {
  // 先给蓝图播放点击动画的机会，再走服务器确认关闭流程。
  BP_OnCloseButtonClicked();
  RequestCloseChest();
}

void UChestWindowWidget::OnTransferAllButtonClicked() {
  // 先给蓝图播放点击动画的机会，再发转移请求。
  BP_OnTransferAllButtonClicked();
  RequestTransferAllFromChest();
}

void UChestWindowWidget::RequestCloseChest() {
  UItemContainer *LiveContainer = Container.Get();
  UContainerInteractionComponent *Interaction =
      ResolveInteractionForActiveContainer(LiveContainer);
  if (!Interaction) {
    return;
  }

  APlayerController *PC = GetOwningPlayer();
  WFLOG_INFO("[宝箱UI] 玩家 %s 请求关闭宝箱窗口（容器=%s）。",
             *GetNameSafe(PC), *GetNameSafe(LiveContainer));

  // 宝箱 Actor 没有 owning connection，不能直接对它发 Server RPC；
  // 必须走玩家身上的交互组件（见类注释与 bug-093）。
  Interaction->Server_RequestCloseContainer(LiveContainer);
}

void UChestWindowWidget::RequestTransferAllFromChest() {
  UItemContainer *LiveContainer = Container.Get();
  UContainerInteractionComponent *Interaction =
      ResolveInteractionForActiveContainer(LiveContainer);
  if (!Interaction) {
    return;
  }

  // 目标背包必须由服务器校验（权限 / 距离）；这里只负责把「玩家自己的背包」
  // 作为目标交给交互组件。绝不从客户端上传任何物品数据。
  APlayerController *PC = GetOwningPlayer();
  APawn *Pawn = PC ? PC->GetPawn() : nullptr;
  UPlayerInventory *PlayerInventory =
      Pawn ? Pawn->FindComponentByClass<UPlayerInventory>() : nullptr;
  if (!PlayerInventory) {
    WFLOG_WARNING("[宝箱UI] 玩家 %s 的 Pawn %s 上找不到 UPlayerInventory，"
                  "无法转移宝箱物品。",
                  *GetNameSafe(PC), *GetNameSafe(Pawn));
    return;
  }

  WFLOG_INFO("[宝箱UI] 玩家 %s 请求把宝箱容器 %s 的全部物品转移到背包 %s。",
             *GetNameSafe(PC), *GetNameSafe(LiveContainer),
             *GetNameSafe(PlayerInventory));

  Interaction->Server_TransferAllFromContainer(LiveContainer, PlayerInventory);
}

UContainerInteractionComponent *
UChestWindowWidget::ResolveInteractionForActiveContainer(
    UItemContainer *LiveContainer) const {
  if (!LiveContainer) {
    // 容器已经被销毁：没有可操作的对象，也无需再发 RPC。
    return nullptr;
  }

  // 关卡切换 / 退出游戏时组件正在析构，此时发 RPC 只会在日志里留下
  // 「pending kill」噪音；直接放弃请求。
  UWorld *World = GetWorld();
  if (!World || World->bIsTearingDown) {
    return nullptr;
  }

  APlayerController *PC = GetOwningPlayer();
  if (!PC) {
    WFLOG_WARNING("[宝箱UI] %s 无法解析交互组件：拿不到 OwningPlayer。",
                  *GetClass()->GetName());
    return nullptr;
  }

  APawn *Pawn = PC->GetPawn();
  UContainerInteractionComponent *Interaction =
      Pawn ? Pawn->FindComponentByClass<UContainerInteractionComponent>()
           : nullptr;
  if (!Interaction) {
    WFLOG_WARNING("[宝箱UI] 玩家 %s 的 Pawn %s 上没有 "
                  "UContainerInteractionComponent，无法与服务器交互。",
                  *GetNameSafe(PC), *GetNameSafe(Pawn));
    return nullptr;
  }

  // 服务器可能已经先一步关闭（例如最后一名打开者离开，或箱子被销毁）：
  // ActiveContainer 已经不是这个容器时，不再重复发请求。
  if (Interaction->GetActiveContainer() != LiveContainer) {
    WFLOG_INFO("[宝箱UI] 当前活动容器已不是 %s，忽略请求"
               "（窗口正在被服务器销毁）。",
               *GetNameSafe(LiveContainer));
    return nullptr;
  }

  return Interaction;
}

AChestActor *UChestWindowWidget::GetChestActor() const {
  // TWeakObjectPtr::Get() 对已销毁 / pending-kill 的对象返回 nullptr，
  // 窗口晚于宝箱析构时也不会把悬空指针交给蓝图。
  return ChestActor.Get();
}

UItemContainer *UChestWindowWidget::GetContainer() const {
  return Container.Get();
}

void UChestWindowWidget::ApplyWindowLayout() {
  // AddToViewport 默认给控件一个铺满屏幕的视口槽（Anchors (0,0)-(1,1)），
  // 不显式设置的话 WBP 根控件会被拉满整块屏幕。这里改成固定尺寸并居中。
  const double SafeWidth = FMath::Max(WindowSize.X, 1.0);
  const double SafeHeight = FMath::Max(WindowSize.Y, 1.0);
  if (!FMath::IsNearlyEqual(SafeWidth, WindowSize.X) ||
      !FMath::IsNearlyEqual(SafeHeight, WindowSize.Y)) {
    WFLOG_WARNING("[宝箱UI] %s 的 WindowSize=(%.1f, %.1f) 含非正值，"
                  "已夹紧为 (%.1f, %.1f)。",
                  *GetClass()->GetName(), WindowSize.X, WindowSize.Y, SafeWidth,
                  SafeHeight);
  }

  // 顺序不能换：SetDesiredSizeInViewport / SetPositionInViewport 内部都会把
  // Anchors 重置为 (0,0)，锚点与对齐必须最后设置才会真正居中
  // （见 UGameViewportSubsystem::SetWidgetSlotDesiredSize / SetWidgetSlotPosition）。
  SetDesiredSizeInViewport(FVector2D(SafeWidth, SafeHeight));
  SetPositionInViewport(FVector2D::ZeroVector);
  SetAnchorsInViewport(FAnchors(0.5f, 0.5f));
  SetAlignmentInViewport(FVector2D(0.5f, 0.5f));

  WFLOG_INFO("[宝箱UI] %s 窗口尺寸 %.0f×%.0f，已按视口中心定位。",
             *GetClass()->GetName(), SafeWidth, SafeHeight);
}

void UChestWindowWidget::NativeDestruct() {
  // 覆盖关闭按钮之外的销毁路径（蓝图 RemoveFromParent、父级被销毁等）：
  // 如果服务器还没关闭，这里补一次请求。已在服务器关闭流程里被销毁时，
  // RequestCloseChest 会因为 ActiveContainer 已空而直接返回，不会重复发 RPC。
  RequestCloseChest();

  Super::NativeDestruct();
}
