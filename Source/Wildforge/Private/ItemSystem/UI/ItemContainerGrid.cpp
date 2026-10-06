#include "ItemSystem/UI/ItemContainerGrid.h"

#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/Pawn.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/UI/InventorySlotWidget.h"

void UItemContainerGrid::NativeConstruct() {
  Super::NativeConstruct();

  // 如果尚未通过 InitializeGrid 设置容器，尝试从拥有者 Pawn 查找
  // （容器是 APlayerCharacter 上的 UPlayerInventory 组件，不在 PlayerController 上）
  if (!Container) {
    if (APawn *Pawn = GetOwningPlayerPawn()) {
      if (UItemContainer *FoundContainer =
              Pawn->FindComponentByClass<UItemContainer>()) {
        InitializeGrid(FoundContainer, SlotsPerRow);
      }
    }
  }

  BindToContainer();
}

void UItemContainerGrid::NativeDestruct() {
  UnbindFromContainer();

  Super::NativeDestruct();
}

void UItemContainerGrid::HandleContainerChanged() {
  // 只处理「结构」：容量可能变了（ResizeContainer 也走这个广播），需要增删槽位控件。
  // 「内容」不在这里刷——每个槽位都订阅了同一个委托，会自己按 (容器, 索引) 拉数据，
  // 并且只在自己那一格真的变了时才碰 Slate，所以这里不要整表刷。
  if (!MainUniformGridPanel) {
    return;
  }

  const int32 Capacity =
      (Container && SlotWidgetClass) ? Container->GetCapacity() : 0;

  // 只有数量真的变了才重排：LayoutSlots 会写每个槽位的 Row/Column 并让布局失效，
  // 每次广播都写一遍就把「按需刷新」的收益又还回去了。
  if (EnsureSlotCount(Capacity)) {
    LayoutSlots();
  }
}

void UItemContainerGrid::BindToContainer() {
  // 先解绑，避免重复订阅
  UnbindFromContainer();

  if (Container) {
    Container->OnContainerChanged.AddDynamic(
        this, &UItemContainerGrid::HandleContainerChanged);
  }
}

void UItemContainerGrid::UnbindFromContainer() {
  if (Container) {
    Container->OnContainerChanged.RemoveDynamic(
        this, &UItemContainerGrid::HandleContainerChanged);
  }
}

void UItemContainerGrid::InitializeGrid(UItemContainer *InContainer,
                                        int32 InSlotsPerRow) {
  if (Container != InContainer) {
    UnbindFromContainer();
    Container = InContainer;
  }
  SlotsPerRow = FMath::Max(1, InSlotsPerRow);

  BindToContainer();
  RefreshGrid();
}

void UItemContainerGrid::SetSlotsPerRow(int32 InSlotsPerRow) {
  SlotsPerRow = FMath::Max(1, InSlotsPerRow);
  RefreshGrid();
}

void UItemContainerGrid::RefreshGrid() {
  if (!MainUniformGridPanel) {
    return;
  }

  // 目标槽位数 = 容器容量（无容器/未指定槽位类时为 0）
  const int32 Capacity =
      (Container && SlotWidgetClass) ? Container->GetCapacity() : 0;

  // 全量路径（初始化 / 换容器 / 改每行个数）：保证数量与布局，
  // 再让每个槽位重新绑定来源并拉一次数据——重复绑到同一来源只会补一次拉取。
  EnsureSlotCount(Capacity);
  LayoutSlots();

  for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index) {
    if (UInventorySlotWidget *SlotWidget = SlotWidgets[Index]) {
      SlotWidget->InitializeSlot(Container, Index);
    }
  }
}

bool UItemContainerGrid::EnsureSlotCount(int32 DesiredCount) {
  if (!MainUniformGridPanel) {
    return false;
  }

  DesiredCount = FMath::Max(0, DesiredCount);
  bool bChanged = false;

  // 容量缩小：从尾部移除多余槽位
  while (SlotWidgets.Num() > DesiredCount) {
    UInventorySlotWidget *Extra = SlotWidgets.Pop(EAllowShrinking::No);
    if (Extra) {
      // 显式退订：控件要等 Slate 资源释放才走 NativeDestruct，
      // 别让容器在这中间继续挂着一个即将销毁的槽位
      Extra->UnbindFromContainer();
      Extra->RemoveFromParent();
    }
    bChanged = true;
  }

  // 容量增大：补齐空槽（位置随后由 LayoutSlots 统一设置）
  while (SlotWidgets.Num() < DesiredCount) {
    if (!SlotWidgetClass) {
      break;
    }
    UInventorySlotWidget *SlotWidget =
        CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), SlotWidgetClass);
    if (!SlotWidget) {
      break;
    }
    const int32 NewIndex = SlotWidgets.Num();
    MainUniformGridPanel->AddChildToUniformGrid(SlotWidget, 0, 0);
    // 新槽位当场绑定来源并拉一次数据：本次广播的调用列表在广播前已经拷贝过
    // （ScriptDelegates.h:924-926），这一轮它自己收不到通知
    SlotWidget->InitializeSlot(Container, NewIndex);
    SlotWidgets.Add(SlotWidget);
    bChanged = true;
  }

  return bChanged;
}

void UItemContainerGrid::LayoutSlots() {
  if (!MainUniformGridPanel) {
    return;
  }

  for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index) {
    UInventorySlotWidget *SlotWidget = SlotWidgets[Index];
    if (!SlotWidget) {
      continue;
    }
    if (UUniformGridSlot *GridSlot = Cast<UUniformGridSlot>(SlotWidget->Slot)) {
      GridSlot->SetRow(Index / SlotsPerRow);
      GridSlot->SetColumn(Index % SlotsPerRow);
    }
  }
}