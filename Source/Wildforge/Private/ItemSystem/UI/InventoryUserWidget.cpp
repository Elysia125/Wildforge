#include "ItemSystem/UI/InventoryUserWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/UI/ItemContainerGrid.h"


void UInventoryUserWidget::NativeConstruct() {
  Super::NativeConstruct();

  // 绑定按钮点击事件
  if (InventoryButton) {
    InventoryButton->OnClicked.AddDynamic(
        this, &UInventoryUserWidget::OnInventoryButtonClicked);
  }

  if (CraftButton) {
    CraftButton->OnClicked.AddDynamic(
        this, &UInventoryUserWidget::OnCraftButtonClicked);
  }

  if (APawn *Pawn = GetOwningPlayerPawn()) {
    if (UItemContainer *FoundContainer =
            Pawn->FindComponentByClass<UItemContainer>()) {
      InitializeInventory(FoundContainer);
    }
  }
  ShowInventoryPage(); // 默认显示背包页
}

void UInventoryUserWidget::InitializeInventory(UItemContainer *InContainer) {
  ItemContainer = InContainer;

  if (ItemContainerGrid && ItemContainer) {
    ItemContainerGrid->InitializeGrid(ItemContainer, 5);
  }
}

void UInventoryUserWidget::SetPlayerName(const FText &InPlayerName) {
  if (PlayerNameText) {
    PlayerNameText->SetText(InPlayerName);
  }
}

void UInventoryUserWidget::ShowInventoryPage() {
  if (CurrentPageIndex == PageIndex_Inventory) {
    return;
  }

  CurrentPageIndex = PageIndex_Inventory;

  if (PageSwitcher) {
    PageSwitcher->SetActiveWidgetIndex(PageIndex_Inventory);
  }

  // 更新按钮视觉状态
  // if (InventoryButton)
  // {
  //     InventoryButton->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 1.f));
  //     // 高亮
  // }
  // if (CraftButton)
  // {
  //     CraftButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
  //     // 变暗
  // }

  OnInventoryPageShown();
}

void UInventoryUserWidget::ShowCraftPage() {
  if (CurrentPageIndex == PageIndex_Craft) {
    return;
  }

  CurrentPageIndex = PageIndex_Craft;

  if (PageSwitcher) {
    PageSwitcher->SetActiveWidgetIndex(PageIndex_Craft);
  }

  if (InventoryButton) {
    InventoryButton->SetBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f, 1.f));
  }
  if (CraftButton) {
    CraftButton->SetBackgroundColor(FLinearColor(1.f, 1.f, 1.f, 1.f));
  }

  OnCraftPageShown();
}

void UInventoryUserWidget::OnInventoryButtonClicked() {
  ShowInventoryPage();
  PlayButtonClickFeedback(InventoryButton);
}

void UInventoryUserWidget::OnCraftButtonClicked() {
  ShowCraftPage();
  PlayButtonClickFeedback(CraftButton);
}

void UInventoryUserWidget::PlayButtonClickFeedback_Implementation(
    UButton *ClickedButton) {
  if (ClickedButton == InventoryButton && InventoryButtonClickAnim) {
    PlayAnimation(InventoryButtonClickAnim);
  } else if (ClickedButton == CraftButton && CraftButtonClickAnim) {
    PlayAnimation(CraftButtonClickAnim);
  }
}