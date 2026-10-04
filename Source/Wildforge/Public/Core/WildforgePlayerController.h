// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "GameFramework/PlayerController.h"
#include "UI/MainUserWidget.h"

#include "WildforgePlayerController.generated.h"

/**
 *
 */
UCLASS()
class WILDFORGE_API AWildforgePlayerController : public APlayerController {
  GENERATED_BODY()
public:
  UPROPERTY(Transient, BlueprintReadWrite, Category = "UI")
  TObjectPtr<UMainUserWidget> MainUserWidget;
  

  UFUNCTION(BlueprintCallable, Category = "UI")
  void ShowInventory();
};
