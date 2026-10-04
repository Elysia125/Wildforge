// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "ItemSystem/UI/InventoryUserWidget.h"

#include "MainUserWidget.generated.h"

/**
 *
 */
UCLASS()
class WILDFORGE_API UMainUserWidget : public UUserWidget {
  GENERATED_BODY()
public:
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UInventoryUserWidget> InventoryWidget;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
  bool bIsInventoryOpen = false;

  UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
  void ShowInventory();

  // 默认实现，函数名必须加 _Implementation
  virtual void ShowInventory_Implementation();
};
