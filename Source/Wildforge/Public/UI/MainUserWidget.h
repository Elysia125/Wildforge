// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "Blueprint/UserWidget.h"
#include "ItemSystem/UI/InventoryUserWidget.h"
#include "ItemSystem/UI/ItemContainerGrid.h"
#include "MainUserWidget.generated.h"

/**
 *
 */
UCLASS()
class WILDFORGE_API UMainUserWidget : public UUserWidget {
  GENERATED_BODY()
public:
  UMainUserWidget(const FObjectInitializer &ObjectInitializer =
                     FObjectInitializer::Get());
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UInventoryUserWidget> InventoryWidget;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UItemContainerGrid> QuickBarWidget;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
  bool bIsInventoryOpen = false;

  UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
  void ShowInventory();

  // 默认实现，函数名必须加 _Implementation
  virtual void ShowInventory_Implementation();
};
