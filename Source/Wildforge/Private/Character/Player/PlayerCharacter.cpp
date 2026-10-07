// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Player/PlayerCharacter.h"

APlayerCharacter::APlayerCharacter() : ABaseCharacter() {
  Inventory = CreateDefaultSubobject<UPlayerInventory>(TEXT("Inventory"));
  Inventory->InitializeContainer(30);
  AttackComponent = CreateDefaultSubobject<UAttackComponent>(TEXT("AttackComponent"));
}
