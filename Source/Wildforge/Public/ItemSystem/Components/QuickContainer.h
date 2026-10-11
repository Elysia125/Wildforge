// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Structs/HotkeyBinding.h"

#include "QuickContainer.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;
/**
 *
 */
UCLASS()
class WILDFORGE_API UQuickContainer : public UItemContainer {
  GENERATED_BODY()
public:
  UQuickContainer();
};
