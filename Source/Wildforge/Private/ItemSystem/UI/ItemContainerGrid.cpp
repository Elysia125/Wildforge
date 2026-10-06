#include "ItemSystem/UI/ItemContainerGrid.h"

#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/Pawn.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Structs/ItemInfo.h"
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

void UItemContainerGrid::HandleContainerChanged() { RefreshGrid(); }

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

  // 维持恒等于容量的槽位数量：不足补建空槽、多余移除
  EnsureSlotCount(Capacity);
  LayoutSlots();

  // 逐个刷新内容，空槽会显示为空
  for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index) {
    UpdateSlot(Index, SlotWidgets[Index]);
  }
}

void UItemContainerGrid::EnsureSlotCount(int32 DesiredCount) {
  if (!MainUniformGridPanel) {
    return;
  }

  DesiredCount = FMath::Max(0, DesiredCount);

  // 容量缩小：从尾部移除多余槽位
  while (SlotWidgets.Num() > DesiredCount) {
    UInventorySlotWidget *Extra = SlotWidgets.Pop(EAllowShrinking::No);
    if (Extra) {
      Extra->RemoveFromParent();
    }
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
    MainUniformGridPanel->AddChildToUniformGrid(SlotWidget, 0, 0);
    SlotWidgets.Add(SlotWidget);
  }
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

void UItemContainerGrid::UpdateSlot(int32 SlotIndex,
                                    UInventorySlotWidget *SlotWidget) {
  if (!SlotWidget || !Container) {
    return;
  }

  FItemInformation ItemInfo;
  if (Container->GetItemAtSlot(SlotIndex, ItemInfo)) {
    SlotWidget->SetItemData(ItemInfo, ItemInfo.ItemQuality);
  } else {
    SlotWidget->ClearSlot();
  }
  SlotWidget->SetOwningContainer(Container);
  SlotWidget->SetSlotIndex(SlotIndex);
}