#include "ItemSystem/UI/InventorySlotWidget.h"

#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ItemSystem/Components/ContainerInteractionComponent.h"
#include "ItemSystem/UI/InventorySlotDragDropOperation.h"
#include "Styling/SlateBrush.h"
#include "UObject/Class.h"
#include "Utils/WildforgeLog.h"

void UInventorySlotWidget::NativeConstruct() {
  Super::NativeConstruct();

  // 构造 / **重建** 完成后，按当前状态重画一次。
  //
  // NativeConstruct 不是「一辈子只跑一次」的：UWidget::MyWidget / MyGCWidget
  // 都是 TWeakPtr（Widget.h:1187、1193），Slate 树只由父级的 slot 持有强引用。
  // 对于没有父级的控件（拖拽视觉就是这种），TakeWidget() 的返回值一被丢弃，
  // 整棵树立刻析构（SObjectWidget::~SObjectWidget → ResetWidget →
  // NativeDestruct +
  // ReleaseSlateResources，SObjectWidget.cpp:42-86），引擎之后再 TakeWidget
  // 就会 重建并**再跑一次本函数**。
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
// NativeConstruct（构造与重建）、SetItemData、ClearSlot 都走它，下面那些单字段
// setter 只被它调用。
void UInventorySlotWidget::RefreshFromState() {
  // 附加文本与本格「有没有物品」无关，两条分支都要重放；
  // 「默认折叠」就落在这里：内容为空 ⇒ 折叠，不依赖 UMG 设计器里的可见性配置
  RefreshButtomExtraText();

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

  // 给蓝图一个机会做额外表现，比如动画、稀有度边框等（数量从 Item.ItemQuality
  // 取）
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

void UInventorySlotWidget::SetButtomExtraText(const FText &InText) {
  // 内容先落到状态上，再统一重画（同 SetItemData）：Slate
  // 控件随时可能被释放并重建， NativeConstruct
  // 会按状态重放，只戳一次控件的写法在重建后会丢。
  ButtomExtraTextContent = InText;

  WFLOG_INFO(
      "InventorySlotWidget::SetButtomExtraText: 槽位 %d 设置附加文本「%s」",
      SlotIndex, *InText.ToString());

  RefreshButtomExtraText();
}

void UInventorySlotWidget::CollapseButtomExtraText() {
  // 清状态 + 重画：之后即使控件被重建，也仍然是折叠的
  // （只改外观不清状态的话，重建会按旧内容又画出来）
  ButtomExtraTextContent = FText::GetEmpty();

  WFLOG_INFO(
      "InventorySlotWidget::CollapseButtomExtraText: 槽位 %d 折叠附加文本",
      SlotIndex);

  RefreshButtomExtraText();
}

// 「附加文本状态 -> ButtomExtraText 外观」的唯一出口：
// 空文本 = 折叠（与 SetTopText / SetBottomText 同一套「空即折叠」语义，
// 「没设过」和「设成空」因此落到同一个外观上，不需要额外的 bool 状态）
void UInventorySlotWidget::RefreshButtomExtraText() {
  if (!ButtomExtraText)
    return;

  const bool bHasText = !ButtomExtraTextContent.IsEmpty();

  ButtomExtraText->SetText(ButtomExtraTextContent);
  ButtomExtraText->SetVisibility(bHasText ? ESlateVisibility::HitTestInvisible
                                          : ESlateVisibility::Collapsed);
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

  // 基准色只抓一次，且必须早于本函数第一次写 BrushColor：
  // 「未选中 / 未高亮」必须还原成**设计器里的样子**，不能是某个硬编码的常量。
  // 否则第一次进入这个状态（拖拽悬停结束）就会永久改掉外观——见 bug-015。
  if (!bNormalSlotColorCached) {
    NormalSlotColor = SlotStyle->GetBrushColor();
    bNormalSlotColorCached = true;
  }

  // 使用 SetBrushColor 来设置边框本身的颜色/透明度。
  // 高亮优先于选中：拖拽悬停的反馈要比选中态更醒目。
  if (bHighlightedState) {
    SlotStyle->SetBrushColor(HighlightColor);
  } else if (bSelectedState) {
    SlotStyle->SetBrushColor(FLinearColor(1.f, 1.f, 1.f, 1.f));
  } else {
    SlotStyle->SetBrushColor(NormalSlotColor);
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
  // 已经绑在同一个来源上：只补一次拉取（RefreshGrid 会重复调到这里）。
  // 比较用弱引用解析值：继承来的失效引用解析为 nullptr，不会被误判成「已绑定」
  if (OwningContainer.Get() == InContainer && SlotIndex == InSlotIndex) {
    RefreshFromContainer();
    return;
  }

  // 换来源：先退订旧的，避免同一个容器上挂两条
  UnbindFromContainer();
  OwningContainer = InContainer;
  SlotIndex = InSlotIndex;

  // 取弱引用解析后的活对象：目标已销毁（含模板带进来的死指针）时为 nullptr，
  // 一律按空槽处理，绝不解引用
  UItemContainer *LiveContainer = OwningContainer.Get();
  if (!LiveContainer || SlotIndex == INDEX_NONE) {
    ClearSlot();
    return;
  }

  // 订阅容器变更：之后容器一变，本控件就去比对「我这一格」有没有变
  LiveContainer->OnContainerChanged.AddDynamic(
      this, &UInventorySlotWidget::HandleContainerChanged);

  // 立即拉一次。两种情况都靠它兜底：
  //   - 控件是刚建出来的：本次广播的调用列表在广播前就被拷贝了
  //     （ScriptDelegates.h:924-926），这一轮轮不到它，必须主动拉；
  //   - Slate 还没构建（如拖拽视觉）：数据先落到状态里，NativeConstruct
  //   会重放出来。
  RefreshFromContainer();
}

void UInventorySlotWidget::UnbindFromContainer() {
  // 只对活对象退订；引用失效时无可退订（弱引用不会解引用已回收对象）
  if (UItemContainer *LiveContainer = OwningContainer.Get()) {
    LiveContainer->OnContainerChanged.RemoveDynamic(
        this, &UInventorySlotWidget::HandleContainerChanged);
  }
}

void UInventorySlotWidget::HandleContainerChanged() { RefreshFromContainer(); }

void UInventorySlotWidget::RefreshFromContainer() {
  // 弱引用解析：容器在本控件存活期间被销毁时，这里拿到 nullptr 而不是悬空指针。
  // 解析值为空且引用确实失效过 ——
  // 说明来源已经没了（可能来自控件模板带进来的旧值），
  // 记一条日志便于定位，然后按空槽处理
  UItemContainer *LiveContainer = OwningContainer.Get();
  if (!LiveContainer && OwningContainer.IsStale(true)) {
    WFLOG_WARNING(
        "InventorySlotWidget::RefreshFromContainer: 绑定过的容器引用已失效"
        "（弱指针，未解引用），本格按空槽处理");
  }

  FItemInformation Item;
  if (!LiveContainer || !LiveContainer->GetItemAtSlot(SlotIndex, Item)) {
    // 空槽：本来就是空的就不要再写一遍 Slate 状态（一次广播会叫到所有槽位）
    if (bHasItemData) {
      ClearSlot();
    }
    return;
  }

  // 本槽数据没变就完全不碰外观：容器只改了一格时，其余槽位的 SetText / SetBrush
  // / SetVisibility
  // 全部省掉——这正是「每格各自刷新」比「整表刷新」省下来的部分。
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
    UItemContainer *LiveContainer = OwningContainer.Get();
    if (LiveContainer == nullptr) {
      return FReply::Handled();
    }
    FItemInformation ItemInfo;
    if (LiveContainer->GetItemAtSlot(SlotIndex, ItemInfo)) {
      if (ItemInfo.ItemID < 0 || ItemInfo.ItemQuality <= 0) {
        return FReply::Handled();
      }

      // 权限：没有访问权限的容器（锁定的宝箱 / 别人的私有箱）不能从这里
      // 发起拖拽。服务器在 RPC 里还会再校验一次，这里只是让本地表现
      // 与权限一致——否则玩家能拖起来，松手后才被服务器静默拒绝。
      if (APlayerController *PC = GetOwningPlayer()) {
        if (!LiveContainer->HasAccess(PC)) {
          WFLOG_WARNING("InventorySlotWidget::NativeOnMouseButtonDown: 玩家 %s "
                        "无权访问容器 %s（槽位 %d），忽略拖拽。",
                        *GetNameSafe(PC), *GetNameSafe(LiveContainer),
                        SlotIndex);
          return FReply::Handled();
        }
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
  UItemContainer *LiveContainer = OwningContainer.Get();
  if (!LiveContainer) {
    return;
  }

  // 与 NativeOnMouseButtonDown 同一道本地权限预检：拖拽可能由蓝图或 Slate
  // 直接触发（不经过鼠标按下），这里必须再挡一次。
  if (APlayerController *PC = GetOwningPlayer()) {
    if (!LiveContainer->HasAccess(PC)) {
      WFLOG_WARNING("InventorySlotWidget::NativeOnDragDetected: 玩家 %s 无权"
                    "访问容器 %s（槽位 %d），不创建拖拽操作。",
                    *GetNameSafe(PC), *GetNameSafe(LiveContainer), SlotIndex);
      return;
    }
  }

  // 1. 创建拖拽操作实例
  UInventorySlotDragDropOperation *DragOp =
      NewObject<UInventorySlotDragDropOperation>(this);

  // 2. 设置拖拽时显示的视觉元素
  // 视觉控件类：默认用「自己这个类」，也就是 WBP_InventorySlot
  // 本身——鼠标下就是一个完整的 格子外观。两种坏值都退回默认：
  //   - 空：WBP 里没指定；
  //   - 指到原生 C++ 类：原生类没有 WidgetTree，RebuildWidget 只返回一个
  //   SSpacer
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
    DragVisual->InitializeSlot(LiveContainer, SlotIndex);
    DragOp->DefaultDragVisual = DragVisual;
  }
  // 设置锚点：鼠标按下位置作为锚点
  DragOp->Pivot = EDragPivot::MouseDown;
  // 拖拽操作上用强引用记录来源容器（拖拽期间保证容器存活；操作本身是短命对象）
  DragOp->SourceContainer = LiveContainer;
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
  // Slate
  // 资源被释放时会走到这里：先退订容器变更，别让容器继续挂着一个已经销毁的控件。
  // （动态委托本身会跳过失效绑定并在广播后压缩列表，ScriptDelegates.h:931/945，
  //  这里退订是为了不留无效条目、也让控件能被正常回收。）
  UnbindFromContainer();

  // 兜底：拖拽途中本控件被销毁（例如容器容量变化导致网格重建）时，
  // 先复位自身不透明度，避免 Slate 资源释放后视觉状态残留。
  SetRenderOpacity(1.f);

  Super::NativeDestruct();
}

// InventorySlotWidget.cpp
bool UInventorySlotWidget::NativeOnDrop(const FGeometry &InGeometry,
                                        const FDragDropEvent &InDragDropEvent,
                                        UDragDropOperation *InOperation) {
  Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);

  UInventorySlotDragDropOperation *ItemOp =
      Cast<UInventorySlotDragDropOperation>(InOperation);
  UItemContainer *LiveContainer = OwningContainer.Get();
  if (!ItemOp || !LiveContainer)
    return false;

  // 先复位源控件的拖拽视觉：无论下面走合并、交换还是提前 return，
  // 都必须恢复，否则源格子会永久停留在半透明状态。
  // 用弱引用兜底 + 源控件自身恢复（Drop/Cancelled/NativeDestruct）双保险。
  ItemOp->RestoreSourceWidget();

  SetHighlight(false);

  // 来源容器用强引用记录在拖拽操作上（拖拽期间保证存活）；
  // 为空说明是损坏的拖拽操作，不能继续。
  UItemContainer *SourceContainer = ItemOp->SourceContainer;
  if (!SourceContainer) {
    WFLOG_WARNING(
        "InventorySlotWidget::NativeOnDrop: 拖拽操作没有来源容器，忽略。");
    return false;
  }

  const int32 FromSlot = ItemOp->SourceSlotIndex;
  const int32 ToSlot = SlotIndex;

  // 源 / 目标槽位已失效（拖拽期间容器变更/整理过），放弃
  if (FromSlot == INDEX_NONE || ToSlot == INDEX_NONE) {
    return false;
  }

  // 拖到自己身上，什么都不做
  if (SourceContainer == LiveContainer && FromSlot == ToSlot) {
    return false;
  }

  // 权限预检（本地）：源与目标都必须允许该玩家访问。这里挡掉的是
  // 「从无权容器拖出 / 拖进锁定箱子」；真正的安全边界在服务器的
  // Server_MoveItemBetweenContainers 里（它会重新校验一次）。
  APlayerController *PC = GetOwningPlayer();
  if (!PC) {
    WFLOG_WARNING(
        "InventorySlotWidget::NativeOnDrop: 控件没有所属玩家，忽略拖拽。");
    return false;
  }
  if (!SourceContainer->HasAccess(PC) || !LiveContainer->HasAccess(PC)) {
    WFLOG_WARNING(
        "InventorySlotWidget::NativeOnDrop: 玩家 %s 无权访问源容器 %s "
        "或目标容器 %s，忽略拖拽。",
        *GetNameSafe(PC), *GetNameSafe(SourceContainer),
        *GetNameSafe(LiveContainer));
    return false;
  }

  // 玩家侧的容器交互组件：跨容器转移的唯一通道。
  // 世界宝箱自己的 Server_* RPC 没有 owning connection，会被引擎静默丢弃
  // （bug-093），所以背包 <-> 快捷栏 / 宝箱的拖拽统一走角色上的这条 RPC。
  if (UContainerInteractionComponent *Interaction =
          GetOwningInteractionComponent()) {
    // 本地距离 / 权限预检：失败就不发 RPC（服务器仍会独立复核）。
    // 同容器拖拽也走这条，保证单机与联机是同一条服务器路径。
    if (!Interaction->CanInteractWith(SourceContainer) ||
        !Interaction->CanInteractWith(LiveContainer)) {
      WFLOG_WARNING("InventorySlotWidget::NativeOnDrop: 本地预检未通过"
                    "（权限 / 距离），不发送转移请求：%s[%d] -> %s[%d]。",
                    *GetNameSafe(SourceContainer), FromSlot,
                    *GetNameSafe(LiveContainer), ToSlot);
      return false;
    }

    WFLOG_INFO("InventorySlotWidget::NativeOnDrop: 请求转移 %s[%d] -> %s[%d]"
               "（玩家=%s）。",
               *GetNameSafe(SourceContainer), FromSlot,
               *GetNameSafe(LiveContainer), ToSlot, *GetNameSafe(PC));
    Interaction->Server_MoveItemBetweenContainers(SourceContainer, FromSlot,
                                                  LiveContainer, ToSlot);
    return true;
  }

  // 兼容路径：没有交互组件（旧蓝图 / 非 APlayerCharacter 的 Pawn）时，
  // 同容器拖拽仍可走容器自己的 Server_MoveOrMerge；跨容器没有安全的
  // 服务器通道，直接拒绝。
  if (SourceContainer == LiveContainer) {
    WFLOG_WARNING("InventorySlotWidget::NativeOnDrop: 没有 "
                  "ContainerInteractionComponent，同容器拖拽回退到 "
                  "Server_MoveOrMerge（%s[%d] -> [%d]）。",
                  *GetNameSafe(LiveContainer), FromSlot, ToSlot);
    LiveContainer->Server_MoveOrMerge(FromSlot, ToSlot);
    return true;
  }

  WFLOG_WARNING(
      "InventorySlotWidget::NativeOnDrop: 没有 "
      "ContainerInteractionComponent，跨容器拖拽被拒绝：%s[%d] -> %s[%d]。",
      *GetNameSafe(SourceContainer), FromSlot, *GetNameSafe(LiveContainer),
      ToSlot);
  return false;
}

bool UInventorySlotWidget::CanAcceptInventoryDrag(
    UDragDropOperation *InOperation) const {
  const UInventorySlotDragDropOperation *ItemOp =
      Cast<UInventorySlotDragDropOperation>(InOperation);
  if (!ItemOp) {
    return false;
  }

  UItemContainer *TargetContainer = OwningContainer.Get();
  UItemContainer *SourceContainer = ItemOp->SourceContainer;
  if (!TargetContainer || !SourceContainer) {
    return false;
  }

  APlayerController *PC = GetOwningPlayer();
  if (!PC) {
    return false;
  }

  // 悬停阶段只查权限，不查距离：距离在落点（CanInteractWith）与服务器
  // RPC 里校验，否则鼠标扫过远处格子会刷满 WARNING。
  return SourceContainer->HasAccess(PC) && TargetContainer->HasAccess(PC);
}

UContainerInteractionComponent *
UInventorySlotWidget::GetOwningInteractionComponent() const {
  APawn *OwningPawn = GetOwningPlayerPawn();
  if (!OwningPawn) {
    return nullptr;
  }
  return OwningPawn->FindComponentByClass<UContainerInteractionComponent>();
}

bool UInventorySlotWidget::NativeOnDragOver(
    const FGeometry &InGeometry, const FDragDropEvent &InDragDropEvent,
    UDragDropOperation *InOperation) {
  // 只接受本系统的拖拽操作，且源 / 目标都必须有访问权限：
  // 其它拖拽（外部资源等）与无权容器之间的拖拽都不接受落点。
  if (CanAcceptInventoryDrag(InOperation)) {
    return true;
  }
  return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

void UInventorySlotWidget::NativeOnDragEnter(
    const FGeometry &InGeometry, const FDragDropEvent &InDragDropEvent,
    UDragDropOperation *InOperation) {
  Super::NativeOnDragEnter(InGeometry, InDragDropEvent, InOperation);

  if (CanAcceptInventoryDrag(InOperation)) {
    SetHighlight(true);
  }
}

void UInventorySlotWidget::NativeOnDragLeave(
    const FDragDropEvent &InDragDropEvent, UDragDropOperation *InOperation) {
  Super::NativeOnDragLeave(InDragDropEvent, InOperation);

  SetHighlight(false);
}