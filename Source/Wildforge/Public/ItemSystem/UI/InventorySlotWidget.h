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
  // 用一整个物品数据刷新槽位
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetItemData(const FItemInformation &Item, int32 Quantity = -1);

  // 单独设置数量，例如 "x100"
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetQuantity(int32 Quantity);

  // 设置耐久 / HP 条
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetItemHP(float CurrentHP, float MaxHP);

  // 设置顶部文字，例如 "DMG 100%"
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetTopText(const FText &InText);

  // 设置底部文字，例如 "30/30"
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetBottomText(const FText &InText);

  // 设置物品图标
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetItemIcon(UTexture2D *InIcon);

  // 设置物品图标 Brush，适合更复杂的样式
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetItemStyle(const FSlateBrush &InBrush);

  // 设置槽位背景 Brush
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetSlotStyle(const FSlateBrush &InBrush);

  // 选中状态，默认用颜色模拟，可改成蓝图实现
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetSelected(bool bSelected);

  // 清空槽位
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void ClearSlot();
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetOwningContainer(UItemContainer *InContainer) {
    OwningContainer = InContainer;
  }
  UItemContainer *GetOwningContainer() const { return OwningContainer; }

  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  void SetSlotIndex(int32 InSlotIndex) { SlotIndex = InSlotIndex; }
  UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
  int32 GetSlotIndex() const { return SlotIndex; }

protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  // 可选：让蓝图决定一些特殊表现，例如动画、选中特效
  UFUNCTION(BlueprintImplementableEvent, Category = "Inventory|Slot")
  void OnItemDataSet(const FItemInformation &Item, int32 Quantity);
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

  /** 高亮状态，用来切换视觉反馈 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Slot")
  void SetHighlight(bool bHighlight);

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

  // 拖拽时显示的 ItemWidget
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|Slot")
  TSubclassOf<UInventorySlotWidget> DragItemWidgetClass;

private:
  UItemContainer *OwningContainer = nullptr;

  int32 SlotIndex = -1;
};