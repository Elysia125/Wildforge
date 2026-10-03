// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CharacterAttributes.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
       BlueprintType, Blueprintable)
class WILDFORGE_API UCharacterAttributes : public UActorComponent {
  GENERATED_BODY()
private:
  UPROPERTY(BlueprintGetter = GetMaxHealth, BlueprintSetter = SetMaxHealth,
            Category = "Attributes")
  float MaxHealth = 100.0f;

  UPROPERTY(BlueprintGetter = GetHealth, BlueprintSetter = SetHealth,
            Category = "Attributes")
  float Health = 100.0f;

public:
  // Sets default values for this component's properties
  UCharacterAttributes();

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetHealthPercent() const { return Health / MaxHealth; }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetHealth() const { return Health; }

  UFUNCTION(BlueprintCallable, Category = "Attributes")
  void SetHealth(float InHealth) { Health = InHealth; }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetMaxHealth() const { return MaxHealth; }

  UFUNCTION(BlueprintCallable, Category = "Attributes")
  void SetMaxHealth(float InHealth) { MaxHealth = InHealth; }
};
