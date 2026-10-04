// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MainUserWidget.h"


void UMainUserWidget::ShowInventory_Implementation() {
  if (InventoryWidget) {
    bIsInventoryOpen = !bIsInventoryOpen;
    InventoryWidget->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Visible
                                                    : ESlateVisibility::Hidden);
  }
}