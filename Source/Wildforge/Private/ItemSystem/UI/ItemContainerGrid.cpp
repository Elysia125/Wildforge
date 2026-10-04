#include "ItemSystem/UI/ItemContainerGrid.h"

#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Structs/ItemInfo.h"
#include "ItemSystem/UI/InventorySlotWidget.h"

DEFINE_LOG_CATEGORY_STATIC(LogItemContainerGrid, Log, All);

namespace {
FString GridCtx(const UUserWidget *Widget) {
  const APlayerController *PC = Widget ? Widget->GetOwningPlayer() : nullptr;
  return FString::Printf(TEXT("Owner=%s Auth=%d"),
                         *GetNameSafe(PC),
                         (PC && PC->HasAuthority()) ? 1 : 0);
}
} // namespace

void UItemContainerGrid::NativeConstruct() {
  Super::NativeConstruct();

  UE_LOG(LogItemContainerGrid, Log,
         TEXT("[Grid] NativeConstruct: %s Pawn=%s 已有Container=%s"),
         *GridCtx(this), *GetNameSafe(GetOwningPlayerPawn()),
         *GetNameSafe(Container.Get()));

  // 如果尚未通过 InitializeGrid 设置容器，尝试从拥有者 Pawn 查找
  // （容器是 APlayerCharacter 上的 UPlayerInventory 组件，不在 PlayerController 上）
  if (!Container) {
    if (APawn *Pawn = GetOwningPlayerPawn()) {
      if (UItemContainer *FoundContainer =
              Pawn->FindComponentByClass<UItemContainer>()) {
        UE_LOG(LogItemContainerGrid, Log,
               TEXT("[Grid] 自动找到容器 %s，执行 InitializeGrid"),
               *GetNameSafe(FoundContainer));
        InitializeGrid(FoundContainer, SlotsPerRow);
      } else {
        UE_LOG(LogItemContainerGrid, Warning,
               TEXT("[Grid] Pawn %s 上未找到 UItemContainer 组件，无法自动绑定"),
               *GetNameSafe(Pawn));
      }
    } else {
      UE_LOG(LogItemContainerGrid, Warning,
             TEXT("[Grid] GetOwningPlayerPawn() 为空，无法自动绑定容器；"
                  "请在蓝图调用 Initialize Grid"));
    }
  }

  BindToContainer();
}

void UItemContainerGrid::NativeDestruct() {
  UnbindFromContainer();

  Super::NativeDestruct();
}

void UItemContainerGrid::HandleContainerChanged() {
  UE_LOG(LogItemContainerGrid, Log,
         TEXT("[Grid] HandleContainerChanged 收到容器变更 -> RefreshGrid "
              "(Capacity=%d)"),
         Container ? Container->GetCapacity() : -1);
  RefreshGrid();
}

void UItemContainerGrid::BindToContainer() {
  // 先解绑，避免重复订阅
  UnbindFromContainer();

  if (Container) {
    Container->OnContainerChanged.AddDynamic(
        this, &UItemContainerGrid::HandleContainerChanged);
    UE_LOG(LogItemContainerGrid, Log,
           TEXT("[Grid] BindToContainer 成功: Container=%s"),
           *GetNameSafe(Container.Get()));
  } else {
    UE_LOG(LogItemContainerGrid, Warning,
           TEXT("[Grid] BindToContainer 跳过: Container 为空（未绑定容器）"));
  }
}

void UItemContainerGrid::UnbindFromContainer() {
  if (Container) {
    Container->OnContainerChanged.RemoveDynamic(
        this, &UItemContainerGrid::HandleContainerChanged);
    UE_LOG(LogItemContainerGrid, Verbose,
           TEXT("[Grid] UnbindFromContainer: Container=%s"),
           *GetNameSafe(Container.Get()));
  }
}

void UItemContainerGrid::InitializeGrid(UItemContainer *InContainer,
                                        int32 InSlotsPerRow) {
  UE_LOG(LogItemContainerGrid, Log,
         TEXT("[Grid] InitializeGrid: Container=%s SlotsPerRow=%d"),
         *GetNameSafe(InContainer), InSlotsPerRow);

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
  UE_LOG(LogItemContainerGrid, Log,
         TEXT("[Grid] RefreshGrid 开始: %s Panel=%s Container=%s Class=%s"),
         *GridCtx(this), MainUniformGridPanel ? TEXT("OK") : TEXT("NULL"),
         *GetNameSafe(Container.Get()),
         *GetNameSafe(SlotWidgetClass.Get()));

  if (!MainUniformGridPanel) {
    UE_LOG(LogItemContainerGrid, Warning,
           TEXT("[Grid] RefreshGrid 中止: MainUniformGridPanel 为空"
                "（UMG 里控件未命名/未 BindWidget？）"));
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

  UE_LOG(LogItemContainerGrid, Log,
         TEXT("[Grid] RefreshGrid 完成: Capacity=%d 槽位Widget数=%d"),
         Capacity, SlotWidgets.Num());
}

void UItemContainerGrid::EnsureSlotCount(int32 DesiredCount) {
  if (!MainUniformGridPanel) {
    return;
  }

  DesiredCount = FMath::Max(0, DesiredCount);

  const int32 Before = SlotWidgets.Num();

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
      UE_LOG(LogItemContainerGrid, Warning,
             TEXT("[Grid] EnsureSlotCount 无法补建: SlotWidgetClass 为空"
                  "（网格未指定槽位 Widget 类）"));
      break;
    }
    UInventorySlotWidget *SlotWidget =
        CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), SlotWidgetClass);
    if (!SlotWidget) {
      UE_LOG(LogItemContainerGrid, Warning,
             TEXT("[Grid] EnsureSlotCount CreateWidget 返回空"));
      break;
    }
    MainUniformGridPanel->AddChildToUniformGrid(SlotWidget, 0, 0);
    SlotWidgets.Add(SlotWidget);
  }

  if (Before != SlotWidgets.Num()) {
    UE_LOG(LogItemContainerGrid, Log,
           TEXT("[Grid] EnsureSlotCount: 目标=%d, 调整 %d -> %d"), DesiredCount,
           Before, SlotWidgets.Num());
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
    const int32 Quantity = ItemInfo.ItemQuality;
    UE_LOG(LogItemContainerGrid, Log,
           TEXT("[Grid] UpdateSlot[%d]: 有物品 ItemID=%d '%s' Icon=%s 数量=%d"),
           SlotIndex, ItemInfo.ItemID, *ItemInfo.ItemName.ToString(),
           ItemInfo.ItemIcon ? TEXT("有") : TEXT("无"), Quantity);
    SlotWidget->SetItemData(ItemInfo, Quantity);
  } else {
    UE_LOG(LogItemContainerGrid, Verbose,
           TEXT("[Grid] UpdateSlot[%d]: 空槽 -> ClearSlot"), SlotIndex);
    SlotWidget->ClearSlot();
  }
}