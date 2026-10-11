#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "Components/Border.h" // 添加这行
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Structs/ItemInfo.h"

#include "InventorySlotWidget.generated.h"

class UImage;
class UTextBlock;
class UProgressBar;
class USizeBox;
class UOverlay;

UCLASS()
class WILDFORGE_API UInventorySlotWidget : public UUserWidget {
  GENERATED_BODY()

public:
  // 选中状态，默认用颜色模拟，可改成蓝图实现（控件自身的视觉状态，与容器数据无关）
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetSelected(bool bSelected);

  // 设置槽位背景 Brush（主题 / 皮肤，与物品数据无关；不会被数据刷新覆盖）
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetSlotStyle(const FSlateBrush &InBrush);

  // —— 左下角附加文本（ButtomExtraText）——
  // 槽位注记（快捷键、价格、状态标签等），与容器数据无关，由外部显式设置。
  // 内容会作为状态存下来（见下方 ButtomExtraTextContent），Slate 重建后仍能重放；
  // 默认内容为空 ⇒ 默认折叠，不需要 UMG 设计器里再配一次可见性。

  /** 设置 ButtomExtraText 的文本并使其可见（传空文本等价于折叠） */
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetButtomExtraText(const FText &InText);

  /** 折叠 ButtomExtraText（默认状态）；清掉内容，重建后也不会再显示 */
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void CollapseButtomExtraText();

  /** 当前 ButtomExtraText 的内容（空 = 没设过 / 已折叠）。
   *  给外部用来避免重复设同一个值：SetButtomExtraText 每次都会写 Slate + 落一条日志，
   *  容器每次广播都重设一遍会把日志刷爆（快捷栏的按键标签就是这么用的）。 */
  UFUNCTION(BlueprintPure, Category = "Inventory|Slot")
  FText GetButtomExtraText() const { return ButtomExtraTextContent; }

  // —— 内部渲染细节，不是外部 API ——
  // 数据只有两个来源：从容器拉（InitializeSlot ->
  // RefreshFromContainer），或从状态重放 （RefreshFromState）。直接戳单个控件的
  // setter 会在下一次容器广播 / Slate 重建时被状态
  // 覆盖回去，所以一律不对外开放。
private:
  // 用一整个物品数据写状态并重画（数量取
  // Item.ItemQuality，本项目数量沿用该字段）
  void SetItemData(const FItemInformation &Item);

  // 清空状态并重画为空槽
  void ClearSlot();

  // 「状态 -> 外观」用到的单字段渲染细节
  void SetQuantity(int32 Quantity);
  void SetItemHP(float CurrentHP, float MaxHP);
  void SetTopText(const FText &InText);
  void SetBottomText(const FText &InText);
  void SetItemIcon(UTexture2D *InIcon);
  void SetItemStyle(const FSlateBrush &InBrush);

  // 「附加文本状态 -> ButtomExtraText 外观」的唯一出口（空文本 = 折叠）
  void RefreshButtomExtraText();

public:
  /**
   * 绑定显示来源：记住 (容器, 索引)、订阅容器变更，并立即拉取一次本槽数据。
   * 之后容器每次广播，本控件只在自己这一格的数据真的变了时才重画。
   * 重复用同一组 (容器, 索引) 调用只补一次拉取，不会重复订阅；
   * 传 nullptr / INDEX_NONE 等于解绑（显示为空槽）。
   * 槽位自己按 (容器, 索引) 取值，所以父级不需要再往控件里推数据
   * （ItemContainer::GetItemAtSlot 是 O(1)
   * 的纯读，客户端读的也是复制的同一份数据）。
   */
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void InitializeSlot(UItemContainer *InContainer, int32 InSlotIndex);

  /** 退订容器变更（换绑前、NativeDestruct 时调用） */
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void UnbindFromContainer();

  /** 按 (Container, SlotIndex) 从容器拉取本槽数据；本槽数据没变则完全不碰外观
   */
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void RefreshFromContainer();

  UItemContainer *GetOwningContainer() const { return OwningContainer.Get(); }

  UFUNCTION(BlueprintPure, Category = "Inventory|Slot")
  int32 GetSlotIndex() const { return SlotIndex; }

protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** 容器内容变化回调：只刷新自己这一格（数据没变就什么都不做） */
  UFUNCTION()
  void HandleContainerChanged();

  // 可选：让蓝图决定一些特殊表现，例如动画、选中特效（数量取 Item.ItemQuality）
  UFUNCTION(BlueprintImplementableEvent, Category = "Inventory|Slot")
  void OnItemDataSet(const FItemInformation &Item);
  virtual FReply
  NativeOnMouseButtonDown(const FGeometry &InGeometry,
                          const FPointerEvent &InMouseEvent) override;
  virtual void NativeOnDragDetected(const FGeometry &InGeometry,
                                    const FPointerEvent &InMouseEvent,
                                    UDragDropOperation *&OutOperation) override;
  virtual bool NativeOnDrop(const FGeometry &InGeometry,
                            const FDragDropEvent &InDragDropEvent,
                            UDragDropOperation *InOperation) override;
  virtual void NativeOnDragCancelled(const FDragDropEvent &InDragDropEvent,
                                     UDragDropOperation *InOperation) override;
  virtual bool NativeOnDragOver(const FGeometry &InGeometry,
                                const FDragDropEvent &InDragDropEvent,
                                UDragDropOperation *InOperation) override;
  virtual void NativeOnDragEnter(const FGeometry &InGeometry,
                                 const FDragDropEvent &InDragDropEvent,
                                 UDragDropOperation *InOperation) override;
  virtual void NativeOnDragLeave(const FDragDropEvent &InDragDropEvent,
                                 UDragDropOperation *InOperation) override;

  /**
   * 高亮状态（拖拽悬停在格子上时触发），用来切换视觉反馈。
   * BlueprintNativeEvent：默认实现写在 C++（给 SlotStyle
   * 染色），蓝图子类可以覆写 SetHighlight 事件替换成自己的表现——覆写后 C++
   * 默认实现不再执行。
   */
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable,
            Category = "Inventory|Slot")
  void SetHighlight(bool bHighlight);

  // 默认实现，函数名必须加 _Implementation（同 UMainUserWidget::ShowInventory）
  virtual void SetHighlight_Implementation(bool bHighlight);
  
protected:
  // 对应 UMG 里的 [TopText]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> TopText;

  // 对应 UMG 里的 [BottomText]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> BottomText;

  // 对应 UMG 里的 [QuantityText]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> QuantityText;

  // 对应 UMG 里的 [ItemHP]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UProgressBar> ItemHP;

  // 对应 UMG 里的 [ItemHpSizeBox]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<USizeBox> ItemHpSizeBox;

  // 对应 UMG 里的 [SlotStyle]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UBorder> SlotStyle;

  // 对应 UMG 里的 [ItemStyle]
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UBorder> ItemStyle;

  // 对应 UMG 里的 OverlayRoot
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UOverlay> OverlayRoot;

  // 对应 UMG 里的 SizeBoxRoot
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<USizeBox> SizeBoxRoot;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> ButtomExtraText;
  // 拖拽时跟随鼠标显示的 ItemWidget；留空则用本控件自身的类（即
  // WBP_InventorySlot）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
  TSubclassOf<UInventorySlotWidget> DragItemWidgetClass;

  // 拖拽悬停高亮时 SlotStyle 的染色（只给 C++ 默认实现用；蓝图覆写 SetHighlight
  // 后即失效）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
  FLinearColor HighlightColor = FLinearColor(1.f, 0.85f, 0.2f, 1.f);

private:
  // 显示来源（容器 + 索引）。必须用弱引用：
  //   1) 控件实例化时会从 WBP 模板逐属性拷贝，`OwningContainer`
  //   这类普通对象引用即使
  //      标了 Transient
  //      也会被带过来（UObjectGlobals.cpp:4671-4684），旧运行的容器指针
  //      会直接落在新实例上；裸指针的第一次解引用就是踩已回收内存（bug-064）；
  //   2) 弱引用只做序列号比对，对已销毁的容器返回「无效」，既不会悬空解引用，
  //      也不会像强引用那样把已死的容器拖在这里（原来注释里担心的问题弱引用天然解决）。
  UPROPERTY(Transient)
  TWeakObjectPtr<UItemContainer> OwningContainer;

  int32 SlotIndex = INDEX_NONE;

  // 选中 /
  // 高亮两个状态分开记录，刷新时再合成，避免后设置的那个把前一个的颜色冲掉
  bool bSelectedState = false;
  bool bHighlightedState = false;

  // 按当前选中 / 高亮状态刷新 SlotStyle 的染色
  void RefreshSlotStyleColor();

  // 「未选中、未高亮」时 SlotStyle
  // 的着色。**不是硬编码常量**：第一次染色前从设计器里的值
  // 抓一次（UBorder::GetBrushColor，Border.cpp:186；UBorder
  // 默认白色，Border.cpp:29）， 这样「没经历过任何状态」的初始外观 ==
  // 状态机算出来的默认外观。 硬编码的教训见 bug-015：那个 0.5f 只在
  // SetHighlight(false) 时才被写进去，而引擎在拖拽 起始/离开时会给格子发
  // OnDragEnter/OnDragLeave（SlateApplication.cpp:3412-3415、5697），
  // 于是第一次拖拽就把格子永久压暗，且再也回不到设计器的样子。
  FLinearColor NormalSlotColor = FLinearColor::White;

  bool bNormalSlotColorCached = false;

  // —— 显示状态（外观的唯一真相源）——
  // 为什么要把数据存下来：Slate 控件会被释放并重建（UWidget::MyWidget /
  // MyGCWidget 都是 TWeakPtr，Widget.h:1187/1193），重建时会**再跑一次
  // NativeConstruct**。所以外观必须能
  // 从状态重放，「构造前/后灌数据」这种一次性的顺序约定是靠不住的。
  // 当前物品数据就是唯一真相源（数量在 ItemQuality 里，不再单独存一份）
  UPROPERTY(Transient)
  FItemInformation CurrentItem;

  bool bHasItemData = false;

  // 左下角附加文本（ButtomExtraText）的内容，空 = 折叠（默认）。
  // 和 CurrentItem 一样按状态存一份：Slate 控件会被重建、NativeConstruct 会重放，
  // 「只戳一次控件」的写法在重建后就丢了。
  // 与容器数据无关：不会被 ClearSlot / 容器刷新清掉，要清请显式调 CollapseButtomExtraText。
  UPROPERTY(Transient)
  FText ButtomExtraTextContent;

  // 按 CurrentItem / bHasItemData
  // 重画整套外观（NativeConstruct、SetItemData、ClearSlot 共用）
  void RefreshFromState();

  // 要显示的数据与当前状态是否完全一致（用来跳过无谓的 Slate 写入）
  bool IsSameAsCurrentData(const FItemInformation &Item) const;
};