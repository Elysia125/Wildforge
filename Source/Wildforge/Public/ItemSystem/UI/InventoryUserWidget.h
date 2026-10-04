#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"

#include "InventoryUserWidget.generated.h"

class UButton;
class UTextBlock;
class UWidgetSwitcher;
class UItemContainerGrid;
class UItemContainer;
class UWidgetAnimation;

UCLASS()
class WILDFORGE_API UInventoryUserWidget : public UUserWidget {
  GENERATED_BODY()

public:
  UFUNCTION(BlueprintCallable, Category = "Inventory")
  void InitializeInventory(UItemContainer *InContainer);

  UFUNCTION(BlueprintCallable, Category = "Inventory")
  void SetPlayerName(const FText &InPlayerName);

  UFUNCTION(BlueprintCallable, Category = "Inventory")
  void ShowInventoryPage();

  UFUNCTION(BlueprintCallable, Category = "Inventory")
  void ShowCraftPage();

  UFUNCTION(BlueprintPure, Category = "Inventory")
  UItemContainerGrid *GetItemContainerGrid() const { return ItemContainerGrid; }

protected:
  virtual void NativeConstruct() override;

  UFUNCTION()
  void OnInventoryButtonClicked();

  UFUNCTION()
  void OnCraftButtonClicked();

  /** 播放按钮点击反馈动画，可在蓝图里实现 */
  UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
  void PlayButtonClickFeedback(UButton *ClickedButton);

  void PlayButtonClickFeedback_Implementation(UButton *ClickedButton);

  UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
  void OnInventoryPageShown();

  UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
  void OnCraftPageShown();

protected:
  // ========== 绑定的控件 ==========
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UButton> InventoryButton;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> InventoryTextBlock;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UButton> CraftButton;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> CraftTextBlock;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UWidgetSwitcher> PageSwitcher;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UItemContainerGrid> ItemContainerGrid;

  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> PlayerNameText;

  UPROPERTY(meta = (BindWidgetAnim), Transient)
  TObjectPtr<UWidgetAnimation> CraftButtonClickAnim;

  UPROPERTY(meta = (BindWidgetAnim), Transient)
  TObjectPtr<UWidgetAnimation> InventoryButtonClickAnim;

private:
  UPROPERTY()
  TObjectPtr<UItemContainer> ItemContainer;

  // 当前选中页索引
  int32 CurrentPageIndex = 0;

  // 页面索引常量
  static constexpr int32 PageIndex_Inventory = 0;
  static constexpr int32 PageIndex_Craft = 1;
};