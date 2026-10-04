#include "ItemSystem/UI/InventorySlotWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

DEFINE_LOG_CATEGORY_STATIC(LogInventorySlot, Log, All);


void UInventorySlotWidget::NativeConstruct() {
  Super::NativeConstruct();

  ClearSlot();
}

void UInventorySlotWidget::SetItemData(const FItemInformation &Item,
                                       int32 Quantity) {
  // 名称 -> TopText（空名称会自动折叠）
  SetTopText(Item.ItemName);

  // 图标 -> ItemStyle（无图标会自动折叠）
  SetItemIcon(Item.ItemIcon);

  // 弹药 -> BottomText 显示“子弹数量/最大子弹数量”（仅使用弹药的装备，如需要装备箭的弓）
  if (Item.UseAmmo) {
    SetBottomText(FText::FromString(
        FString::Printf(TEXT("%d/%d"), Item.Ammo, Item.AmmoMax)));
  } else {
    SetBottomText(FText::GetEmpty());
  }

  // 当前数量 -> QuantityText（数量 <= 1 时折叠）
  SetQuantity(Quantity);

  // 耐久 -> ItemHP（最大耐久 > 0 时才显示）
  SetItemHP(static_cast<float>(Item.ItemCurHP),
            static_cast<float>(Item.ItemMaxHP));

  const FString NameStr =
      TopText ? TopText->GetText().ToString() : FString(TEXT("<null>"));
  const FString AmmoStr =
      Item.UseAmmo ? FString::Printf(TEXT("%d/%d"), Item.Ammo, Item.AmmoMax)
                   : FString(TEXT("<隐藏>"));
  const FString QtyStr = Quantity > 1
                             ? FString::Printf(TEXT("x%d"), Quantity)
                             : FString(TEXT("<隐藏>"));
  UE_LOG(LogInventorySlot, Log,
         TEXT("[Slot] SetItemData: '%s' 数量=%d 图标=%s 名称=%s 弹药(Bottom)=%s "
              "数量= %s 耐久=%s"),
         *Item.ItemName.ToString(), Quantity,
         Item.ItemIcon ? TEXT("有") : TEXT("无"), *NameStr, *AmmoStr, *QtyStr,
         Item.ItemMaxHP > 0 ? TEXT("显示") : TEXT("<隐藏>"));

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
    ItemHP->SetPercent(bHasDurability
                           ? FMath::Clamp(CurrentHP / MaxHP, 0.f, 1.f)
                           : 0.f);
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
  BottomText->SetVisibility(InText.IsEmpty() ? ESlateVisibility::Collapsed
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
  SlotStyle->SetBrushColor( 
      bSelected ? FLinearColor(1.f, 1.f, 1.f, 1.f) : FLinearColor(1.f, 1.f, 1.f, 0.5f));
}

void UInventorySlotWidget::ClearSlot() {
  UE_LOG(LogInventorySlot, Verbose,
         TEXT("[Slot] ClearSlot: 控件全部折叠 -> %s"), *GetName());

  // 各 setter 会根据“空/无”自动折叠对应控件，形成默认不可见状态
  SetTopText(FText::GetEmpty());
  SetBottomText(FText::GetEmpty());
  SetQuantity(0);
  SetItemIcon(nullptr);
  SetItemHP(0.f, 0.f);
}