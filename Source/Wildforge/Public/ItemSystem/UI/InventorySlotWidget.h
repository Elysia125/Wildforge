#pragma once

#include "CoreMinimal.h"
#include "Components/Border.h"   // 添加这行
#include "Blueprint/UserWidget.h"
#include "ItemSystem/Structs/ItemInfo.h"
#include "InventorySlotWidget.generated.h"

class UImage;
class UTextBlock;
class UProgressBar;
class USizeBox;
class UOverlay;

UCLASS()
class WILDFORGE_API UInventorySlotWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // 用一整个物品数据刷新槽位
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetItemData(const FItemInformation& Item, int32 Quantity = 1);

    // 单独设置数量，例如 "x100"
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetQuantity(int32 Quantity);

    // 设置耐久 / HP 条
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetItemHP(float CurrentHP, float MaxHP);

    // 设置顶部文字，例如 "DMG 100%"
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetTopText(const FText& InText);

    // 设置底部文字，例如 "30/30"
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetBottomText(const FText& InText);

    // 设置物品图标
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetItemIcon(UTexture2D* InIcon);

    // 设置物品图标 Brush，适合更复杂的样式
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetItemStyle(const FSlateBrush& InBrush);

    // 设置槽位背景 Brush
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetSlotStyle(const FSlateBrush& InBrush);

    // 选中状态，默认用颜色模拟，可改成蓝图实现
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void SetSelected(bool bSelected);

    // 清空槽位
    UFUNCTION(BlueprintCallable, Category = "Inventory|Slot")
    void ClearSlot();

protected:
    virtual void NativeConstruct() override;

    // 可选：让蓝图决定一些特殊表现，例如动画、选中特效
    UFUNCTION(BlueprintImplementableEvent, Category = "Inventory|Slot")
    void OnItemDataSet(const FItemInformation& Item, int32 Quantity);

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
};