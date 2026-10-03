// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Character/Components/CharacterAttributes.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BaseCharacter.generated.h"


UCLASS(ClassGroup = (Custom),BlueprintType, Blueprintable)
class WILDFORGE_API ABaseCharacter : public ACharacter {
  GENERATED_BODY()
public:
  // Sets default values for this character's properties
  ABaseCharacter();

private:
  UPROPERTY(BlueprintGetter = GetCharacterAttributes, Category = "Attributes")
  TObjectPtr<UCharacterAttributes> CharacterAttributes;

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;

public:
  // Called every frame
  virtual void Tick(float DeltaTime) override;

  // Called to bind functionality to input
  virtual void SetupPlayerInputComponent(
      class UInputComponent *PlayerInputComponent) override;

  UFUNCTION(BlueprintPure, Category = "Attributes",meta = (BlueprintThreadSafe))
  UCharacterAttributes* GetCharacterAttributes() const {
    return CharacterAttributes.Get();
  }
};
