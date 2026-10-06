#include "ItemSystem/UI/InventorySlotWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "ItemSystem/UI/InventorySlotDragDropOperation.h"
#include "Styling/SlateBrush.h"

void UInventorySlotWidget::NativeConstruct() {
  Super::NativeConstruct();

  ClearSlot();
  DragItemWidgetClass = UInventorySlotWidget::StaticClass();
}

void UInventorySlotWidget::SetItemData(const FItemInformation &Item,
                                       int32 Quantity) {
  // 当前数量 -> QuantityText（数量 <= 1 时折叠）
  if (Quantity < 0) {
    Quantity = Item.ItemQuality;
  }
  if (Quantity <= 1) {
    return;
  }
  // 名称 -> TopText（空名称会自动折叠）
  SetTopText(Item.ItemName);

  // 图标 -> ItemStyle（无图标会自动折叠）
  SetItemIcon(Item.ItemIcon);

  // 弹药 -> BottomText
  // 显示“子弹数量/最大子弹数量”（仅使用弹药的装备，如需要装备箭的弓）
  if (Item.UseAmmo) {
    SetBottomText(FText::FromString(
        FString::Printf(TEXT("%d/%d"), Item.Ammo, Item.AmmoMax)));
  } else {
    SetBottomText(FText::GetEmpty());
  }
  SetQuantity(Quantity);

  // 耐久 -> ItemHP（最大耐久 > 0 时才显示）
  SetItemHP(static_cast<float>(Item.ItemCurHP),
            static_cast<float>(Item.ItemMaxHP));

  // 给蓝图一个机会做额外表现，比如动画、稀有度边框等
  OnItemDataSet(Item, Quantity);
}

void UInventorySlotWidget::SetQuantity(int32 Quantity) {
  if (!QuantityText)
    return;

  if (Quantity > 1) {
    QuantityText->SetText(
        FText::FromString(FString::Printf(TEXT("x%d"), Quantity)));
    QuantityText->SetVisibility(ESlateVisibility::HitTestInvisible);
  } else {
    QuantityText->SetText(FText::GetEmpty());
    QuantityText->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void UInventorySlotWidget::SetItemHP(float CurrentHP, float MaxHP) {
  const bool bHasDurability = MaxHP > 0.f;

  if (ItemHP) {
    ItemHP->SetPercent(
        bHasDurability ? FMath::Clamp(CurrentHP / MaxHP, 0.f, 1.f) : 0.f);
    ItemHP->SetVisibility(bHasDurability ? ESlateVisibility::HitTestInvisible
                                         : ESlateVisibility::Collapsed);
  }

  if (ItemHpSizeBox) {
    ItemHpSizeBox->SetVisibility(bHasDurability
                                     ? ESlateVisibility::HitTestInvisible
                                     : ESlateVisibility::Collapsed);
  }
}

void UInventorySlotWidget::SetTopText(const FText &InText) {
  if (!TopText)
    return;

  TopText->SetText(InText);
  TopText->SetVisibility(InText.IsEmpty() ? ESlateVisibility::Collapsed
                                          : ESlateVisibility::HitTestInvisible);
}

void UInventorySlotWidget::SetBottomText(const FText &InText) {
  if (!BottomText)
    return;

  BottomText->SetText(InText);
  BottomText->SetVisibility(InText.IsEmpty()
                                ? ESlateVisibility::Collapsed
                                : ESlateVisibility::HitTestInvisible);
}

void UInventorySlotWidget::SetItemIcon(UTexture2D *InIcon) {
  if (!ItemStyle)
    return;

  if (InIcon) {
    ItemStyle->SetBrushFromTexture(InIcon);
    ItemStyle->SetVisibility(ESlateVisibility::HitTestInvisible);
  } else {
    ItemStyle->SetBrush(FSlateBrush());
    ItemStyle->SetVisibility(ESlateVisibility::Collapsed);
  }
}

void UInventorySlotWidget::SetItemStyle(const FSlateBrush &InBrush) {
  if (ItemStyle) {
    ItemStyle->SetBrush(InBrush);
  }
}

void UInventorySlotWidget::SetSlotStyle(const FSlateBrush &InBrush) {
  if (SlotStyle) {
    SlotStyle->SetBrush(InBrush);
  }
}

void UInventorySlotWidget::SetSelected(bool bSelected) {
  if (!SlotStyle)
    return;

  // 使用 SetBrushColor 来设置边框本身的颜色/透明度
  SlotStyle->SetBrushColor(bSelected ? FLinearColor(1.f, 1.f, 1.f, 1.f)
                                     : FLinearColor(1.f, 1.f, 1.f, 0.5f));
}

void UInventorySlotWidget::ClearSlot() {
  // 各 setter 会根据“空/无”自动折叠对应控件，形成默认不可见状态
  SetTopText(FText::GetEmpty());
  SetBottomText(FText::GetEmpty());
  SetQuantity(0);
  SetItemIcon(nullptr);
  SetItemHP(0.f, 0.f);
}

FReply UInventorySlotWidget::NativeOnMouseButtonDown(
    const FGeometry &InGeometry, const FPointerEvent &InMouseEvent) {
  if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton) {
    if (OwningContainer == nullptr) {
      return FReply::Handled();
    }
    FItemInformation ItemInfo;
    if (OwningContainer->GetItemAtSlot(SlotIndex, ItemInfo)) {
      if (ItemInfo.ItemID < 0 || ItemInfo.ItemQuality <= 0) {
        return FReply::Handled();
      }
      // 鼠标左键按下时，调用拖动逻辑
      return FReply::Handled().DetectDrag(TakeWidget(), EKeys::LeftMouseButton);
    } else {
      return FReply::Handled();
    }
  }
  return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UInventorySlotWidget::NativeOnDragDetected(
    const FGeometry &InGeometry, const FPointerEvent &InMouseEvent,
    UDragDropOperation *&OutOperation) {
  Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

  // 1. 创建拖拽操作实例
  UInventorySlotDragDropOperation *DragOp =
      NewObject<UInventorySlotDragDropOperation>(this);

  // 2. 设置拖拽时显示的视觉元素
  // 设置拖拽视觉
  if (DragItemWidgetClass) {
    UInventorySlotWidget *DragVisual = CreateWidget<UInventorySlotWidget>(
        GetOwningPlayer(), DragItemWidgetClass);
    if (DragVisual) {
      FItemInformation ItemInfo;
      if (OwningContainer->GetItemAtSlot(SlotIndex, ItemInfo)) {
        DragVisual->SetItemData(ItemInfo);
      }
      DragOp->DefaultDragVisual = DragVisual;
    }
  }
  // 设置锚点：鼠标按下位置作为锚点
  DragOp->Pivot = EDragPivot::MouseDown;
  DragOp->SourceContainer = OwningContainer;
  DragOp->SourceSlotIndex = SlotIndex;
  // 弱引用记录源控件：供落点/取消时兜底复位显示（不延长控件生命周期）
  DragOp->SourceWidget = this;
  // 本地视觉反馈：拖拽时半透明
  SetRenderOpacity(0.5f);
  // 可选：额外偏移
  // DragOp->Offset = FVector2D(10.f, 10.f);

  // 3. 将创建的操作赋值给OutOperation参数，完成拖拽发起
  OutOperation = DragOp;
}

void UInventorySlotWidget::NativeOnDragCancelled(
    const FDragDropEvent &InDragDropEvent, UDragDropOperation *InOperation) {
  Super::NativeOnDragCancelled(InDragDropEvent, InOperation);

  // 恢复显示
  SetRenderOpacity(1.f);
}

void UInventorySlotWidget::NativeDestruct() {
  // 兜底：拖拽途中本控件被销毁（例如容器容量变化导致网格重建）时，
  // 先复位自身不透明度，避免 Slate 资源释放后视觉状态残留。
  SetRenderOpacity(1.f);

  Super::NativeDestruct();
}

// InventorySlotWidget.cpp
bool UInventorySlotWidget::NativeOnDrop(const FGeometry& InGeometry,
                                        const FDragDropEvent& InDragDropEvent,
                                        UDragDropOperation* InOperation)
{
    Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);

    UInventorySlotDragDropOperation* ItemOp = Cast<UInventorySlotDragDropOperation>(InOperation);
    if (!ItemOp || !OwningContainer) return false;

    // 先复位源控件的拖拽视觉：无论下面走合并、交换还是提前 return，
    // 都必须恢复，否则源格子会永久停留在半透明状态。
    // 用弱引用兜底 + 源控件自身恢复（Drop/Cancelled/NativeDestruct）双保险。
    ItemOp->RestoreSourceWidget();

    SetHighlight(false);

    const int32 FromSlot = ItemOp->SourceSlotIndex;
    const int32 ToSlot = SlotIndex;

    // 源槽位已失效（拖拽期间容器变更/整理过），放弃
    if (FromSlot == INDEX_NONE || ToSlot == INDEX_NONE)
    {
        return false;
    }

    // 拖到自己身上，什么都不做
    if (ItemOp->SourceContainer == OwningContainer && FromSlot == ToSlot)
    {
        return false;
    }

    // 跨容器拖拽暂不支持：共享容器（箱子等）需要额外的服务器校验通道
    if (ItemOp->SourceContainer != OwningContainer)
    {
        return false;
    }

    // 真正的合并/交换规则由容器决定（服务器权威，客户端不得直接改容器数据）：
    //   - 同 ItemID 且可堆叠且目标未满堆 -> 源数量并入目标，装不下的留在源槽位
    //   - 不可堆叠 / 不同 ItemID / 目标已是满堆 -> 交换两槽
    OwningContainer->Server_MoveOrMerge(FromSlot, ToSlot);

    return true;
}

bool UInventorySlotWidget::NativeOnDragOver(const FGeometry& InGeometry,
                                            const FDragDropEvent& InDragDropEvent,
                                            UDragDropOperation* InOperation)
{
    // 只接受本系统的拖拽操作，其它拖拽（外部资源等）不接受落点
    if (Cast<UInventorySlotDragDropOperation>(InOperation))
    {
        return true;
    }
    return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

void UInventorySlotWidget::NativeOnDragEnter(const FGeometry& InGeometry,
                                             const FDragDropEvent& InDragDropEvent,
                                             UDragDropOperation* InOperation)
{
    Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

    if (Cast<UInventorySlotDragDropOperation>(InOperation))
    {
        SetHighlight(true);
    }
}

void UInventorySlotWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
    Super::NativeOnDragLeave(InDragDropEvent, InOperation);

    SetHighlight(false);
}