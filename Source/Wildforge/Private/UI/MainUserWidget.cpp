// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MainUserWidget.h"
#include "ItemSystem/Components/QuickContainer.h"

UMainUserWidget::UMainUserWidget(const FObjectInitializer &ObjectInitializer)
    : Super(ObjectInitializer) {
  if(QuickBarWidget){
      //  查找容器初始化
    if (APawn *Pawn = GetOwningPlayerPawn()) {
      if (UItemContainer *FoundContainer =
              Pawn->FindComponentByClass<UQuickContainer>()) {
                QuickBarWidget->SetSlotsPerRow(FoundContainer->GetCapacity());
        QuickBarWidget->InitializeGrid(FoundContainer);
      }
    }
  }
}

void UMainUserWidget::ShowInventory_Implementation() {
  if (InventoryWidget) {
    bIsInventoryOpen = !bIsInventoryOpen;
    InventoryWidget->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Visible
                                                    : ESlateVisibility::Hidden);
  }
}