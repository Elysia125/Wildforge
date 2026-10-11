// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Components/QuickContainer.h"
#include "ItemSystem/enums/ContainerType.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

UQuickContainer::UQuickContainer() : UItemContainer() {
  setContainerType(EContainerType::PlayerHotbar);
  PrimaryComponentTick.bCanEverTick = false;
}