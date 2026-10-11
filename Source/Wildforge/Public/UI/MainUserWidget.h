// Fill out your copyright notice in the Description page of Project Settings.

#pragma once


#include "Blueprint/UserWidget.h"
#include "ItemSystem/UI/InventoryUserWidget.h"
#include "ItemSystem/UI/QuickBarWidget.h"

#include "MainUserWidget.generated.h"

/**
 *
 */
UCLASS()
class WILDFORGE_API UMainUserWidget : public UUserWidget {
  GENERATED_BODY()
protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** 占有关系变化（含客户端「Pawn 复制到位后被占有」）时重试初始化快捷栏。
   *  客户端上本地 PlayerController 的 BeginPlay 往往早于 Pawn 到位，
   *  那一刻 GetOwningPlayerPawn() 还是空，不补这一枪快捷栏就整块不工作。 */
  UFUNCTION()
  void HandlePossessedPawnChanged(APawn *OldPawn, APawn *NewPawn);

  /** 找 Pawn 上的 UQuickContainer 并交给 QuickBarWidget。
   *  容器容量此刻**可能还是 0**（见 .cpp 里的时序说明），这是正常的：
   *  容量由容器自己广播 OnContainerChanged 时补课，这里不拿它做条件判断。 */
  void TryInitializeQuickBar();

public:
  
  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UInventoryUserWidget> InventoryWidget;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UQuickBarWidget> QuickBarWidget;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
  bool bIsInventoryOpen = false;

  UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
  void ShowInventory();

  // 默认实现，函数名必须加 _Implementation
  virtual void ShowInventory_Implementation();
};
