// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "../BaseCharacter.h"
#include "Character/Components/AttackComponent.h"
#include "ItemSystem/Components/PlayerInventory.h"

#include "PlayerCharacter.generated.h"

/**
 * 玩家角色
 */
UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable)
class WILDFORGE_API APlayerCharacter : public ABaseCharacter {
  GENERATED_BODY()
private:
  UPROPERTY(BlueprintGetter = GetInventory, Category = "Items")
  TObjectPtr<UPlayerInventory> Inventory;

protected:
  UPROPERTY(EditAnywhere, Category = "Items")
  TObjectPtr<UAttackComponent> AttackComponent;

public:
  // Sets default values for this character's properties
  APlayerCharacter();

  UFUNCTION(BlueprintPure, Category = "Items", meta = (BlueprintThreadSafe))
  UPlayerInventory *GetInventory() const { return Inventory.Get(); }
};
