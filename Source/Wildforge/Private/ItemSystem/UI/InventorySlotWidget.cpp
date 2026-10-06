#include "ItemSystem/UI/InventorySlotWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "ItemSystem/UI/InventorySlotDragDropOperation.h"
#include "Styling/SlateBrush.h"
#include "UObject/Class.h"

void UInventorySlotWidget::NativeConstruct() {
  Super::NativeConstruct();

  // 构造 / **重建** 完成后，按当前状态重画一次。
  //
  // NativeConstruct 不是「一辈子只跑一次」的：UWidget::MyWidget / MyGCWidget 都是
  // TWeakPtr（Widget.h:1187、1193），Slate 树只由父级的 slot 持有强引用。
  // 对于没有父级的控件（拖拽视觉就是这种），TakeWidget() 的返回值一被丢弃，
  // 整棵树立刻析构（SObjectWidget::~SObjectWidget → ResetWidget → NativeDestruct +
  // ReleaseSlateResources，SObjectWidget.cpp:42-86），引擎之后再 TakeWidget 就会
  // 重建并**再跑一次本函数**。
  //
  // 所以这里不能盲清数据（会把 SetItemData 刚写进去的状态清掉），
  // 而是「有数据就画数据、没数据就画空槽」——顺序无关，重建安全。
  RefreshFromState();
}

void UInventorySlotWidget::SetItemData(const FItemInformation &Item) {
  // 数据整体落到状态上，再重画：Slate 控件随时可能被释放并重建，
  // 外观必须能从状态重放，不能只依赖「构造之后灌数据」这一次性的顺序。
  // 数量就是 Item.ItemQuality（本项目数量沿用该字段），不再单独传/存第二份。
  CurrentItem = Item;
  bHasItemData = true;

  RefreshFromState();
}

// 按 CurrentItem / bHasItemData 重画整套外观。「状态 -> 外观」只有这一个出口：
// NativeConstruct（构造与重建）、SetItemData、ClearSlot 都走它，下面那些单字段 setter 只被它调用。
void UInventorySlotWidget::RefreshFromState() {
  // 空槽：各 setter 会根据“空/无”自动折叠对应控件，形成默认不可见状态
  if (!bHasItemData) {
    SetTopText(FText::GetEmpty());
    SetBottomText(FText::GetEmpty());
    SetQuantity(0);
    SetItemIcon(nullptr);
    SetItemHP(0.f, 0.f);
    return;
  }

  // 名称 -> TopText（空名称会自动折叠）
  SetTopText(CurrentItem.ItemName);

  // 图标 -> ItemStyle（无图标会自动折叠）
  SetItemIcon(CurrentItem.ItemIcon);

  // 弹药 -> BottomText
  // 显示“子弹数量/最大子弹数量”（仅使用弹药的装备，如需要装备箭的弓）
  if (CurrentItem.UseAmmo) {
    SetBottomText(FText::FromString(
        FString::Printf(TEXT("%d/%d"), CurrentItem.Ammo, CurrentItem.AmmoMax)));
  } else {
    SetBottomText(FText::GetEmpty());
  }
  // 当前数量 -> QuantityText（数量 <= 1 时由 SetQuantity 折叠）
  SetQuantity(CurrentItem.ItemQuality);

  // 耐久 -> ItemHP（最大耐久 > 0 时才显示）
  SetItemHP(static_cast<float>(CurrentItem.ItemCurHP),
            static_cast<float>(CurrentItem.ItemMaxHP));

  // 给蓝图一个机会做额外表现，比如动画、稀有度边框等（数量从 Item.ItemQuality 取）
  OnItemDataSet(CurrentItem);
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
  bSelectedState = bSelected;
  RefreshSlotStyleColor();
}

void UInventorySlotWidget::SetHighlight_Implementation(bool bHighlight) {
  bHighlightedState = bHighlight;
  RefreshSlotStyleColor();
}

void UInventorySlotWidget::RefreshSlotStyleColor() {
  if (!SlotStyle)
    return;

  // 使用 SetBrushColor 来设置边框本身的颜色/透明度。
  // 高亮优先于选中：拖拽悬停的反馈要比选中态更醒目。
  if (bHighlightedState) {
    SlotStyle->SetBrushColor(HighlightColor);
  } else {
    SlotStyle->SetBrushColor(bSelectedState ? FLinearColor(1.f, 1.f, 1.f, 1.f)
                                            : FLinearColor(1.f, 1.f, 1.f, 0.5f));
  }
}

void UInventorySlotWidget::ClearSlot() {
  // 清状态 + 重画：这样之后即使控件被重建，也仍然是空槽
  // （只改外观不清状态的话，重建会把旧物品又画回来）
  bHasItemData = false;
  CurrentItem = FItemInformation();

  RefreshFromState();
}

void UInventorySlotWidget::InitializeSlot(UItemContainer *InContainer,
                                          int32 InSlotIndex) {
  // 已经绑在同一个来源上：只补一次拉取（RefreshGrid 会重复调到这里）
  if (OwningContainer == InContainer && SlotIndex == InSlotIndex) {
    RefreshFromContainer();
    return;
  }

  // 换来源：先退订旧的，避免同一个容器上挂两条
  UnbindFromContainer();
  OwningContainer = InContainer;
  SlotIndex = InSlotIndex;

  if (!OwningContainer || SlotIndex == INDEX_NONE) {
    ClearSlot();
    return;
  }

  // 订阅容器变更：之后容器一变，本控件就去比对「我这一格」有没有变
  OwningContainer->OnContainerChanged.AddDynamic(
      this, &UInventorySlotWidget::HandleContainerChanged);

  // 立即拉一次。两种情况都靠它兜底：
  //   - 控件是刚建出来的：本次广播的调用列表在广播前就被拷贝了
  //     （ScriptDelegates.h:924-926），这一轮轮不到它，必须主动拉；
  //   - Slate 还没构建（如拖拽视觉）：数据先落到状态里，NativeConstruct 会重放出来。
  RefreshFromContainer();
}

void UInventorySlotWidget::UnbindFromContainer() {
  if (OwningContainer) {
    OwningContainer->OnContainerChanged.RemoveDynamic(
        this, &UInventorySlotWidget::HandleContainerChanged);
  }
}

void UInventorySlotWidget::HandleContainerChanged() { RefreshFromContainer(); }

void UInventorySlotWidget::RefreshFromContainer() {
  FItemInformation Item;
  if (!OwningContainer || !OwningContainer->GetItemAtSlot(SlotIndex, Item)) {
    // 空槽：本来就是空的就不要再写一遍 Slate 状态（一次广播会叫到所有槽位）
    if (bHasItemData) {
      ClearSlot();
    }
    return;
  }

  // 本槽数据没变就完全不碰外观：容器只改了一格时，其余槽位的 SetText / SetBrush /
  // SetVisibility 全部省掉——这正是「每格各自刷新」比「整表刷新」省下来的部分。
  if (IsSameAsCurrentData(Item)) {
    return;
  }

  SetItemData(Item);
}

bool UInventorySlotWidget::IsSameAsCurrentData(
    const FItemInformation &Item) const {
  if (!bHasItemData) {
    return false;
  }

  // 交给反射逐属性比较（数量也在结构体里，不用单独比），而不是手写一长串字段比较：
  // 结构体以后加字段也不用回来维护。
  const UScriptStruct *Struct = FItemInformation::StaticStruct();
  return Struct != nullptr &&
         Struct->CompareScriptStruct(&CurrentItem, &Item, PPF_None);
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

  // 没有容器就没有可拖拽的内容来源，直接放弃（否则下面会解引用空指针）
  if (!OwningContainer) {
    return;
  }

  // 1. 创建拖拽操作实例
  UInventorySlotDragDropOperation *DragOp =
      NewObject<UInventorySlotDragDropOperation>(this);

  // 2. 设置拖拽时显示的视觉元素
  // 视觉控件类：默认用「自己这个类」，也就是 WBP_InventorySlot 本身——鼠标下就是一个完整的
  // 格子外观。两种坏值都退回默认：
  //   - 空：WBP 里没指定；
  //   - 指到原生 C++ 类：原生类没有 WidgetTree，RebuildWidget 只返回一个 SSpacer
  //     （UserWidget.cpp:1203），表现是「鼠标下什么都没有」而不是报错。
  TSubclassOf<UInventorySlotWidget> VisualClass = DragItemWidgetClass;
  if (!VisualClass || VisualClass == UInventorySlotWidget::StaticClass()) {
    VisualClass = GetClass();
  }

  if (UInventorySlotWidget *DragVisual =
          CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), VisualClass)) {
    // 走和普通格子完全一样的数据路径：绑定 (容器, 索引)，然后由控件自己拉数据。
    // 同样**不要**在这里 TakeWidget()：这个控件没有父级持有 Slate 引用，
    // 手工 TakeWidget 拿到的 TSharedRef 一丢就整棵析构，引擎随后还会再建一次
    // （FUMGDragDropOp::New 里的 TakeWidget，UMGDragDropOp.cpp:192）；
    // 外观由 NativeConstruct 按状态重放，顺序无关。
    DragVisual->InitializeSlot(OwningContainer, SlotIndex);
    DragOp->DefaultDragVisual = DragVisual;
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
  // Slate 资源被释放时会走到这里：先退订容器变更，别让容器继续挂着一个已经销毁的控件。
  // （动态委托本身会跳过失效绑定并在广播后压缩列表，ScriptDelegates.h:931/945，
  //  这里退订是为了不留无效条目、也让控件能被正常回收。）
  UnbindFromContainer();

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