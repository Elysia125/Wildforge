// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "Animation/AnimMontage.h"

#include "LandRollComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLandRollStarted);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnLandRollFinished);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API ULandRollComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  ULandRollComponent();

protected:
  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;
  // Called when the game starts
  virtual void BeginPlay() override;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LandRoll")
  UAnimMontage *LandRollMontage;
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  float MontagePlayRate = 1.5f;
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  float LandRollCooldown = 1.0f;
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  float LastLandRollTime = 0.0f;
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "LandRoll")
  bool bIsRolling = false;

  UFUNCTION(BlueprintNativeEvent, BlueprintAuthorityOnly, Category = "LandRoll")
  bool PlayLandRollMontage();

  bool PlayLandRollMontageInternal();

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "LandRoll")
  void OnLandRollMontageFinished(UAnimMontage *Montage, bool bInterrupted);

  UPROPERTY (BlueprintAssignable, Category = "LandRoll")
  FOnLandRollStarted OnLandRollStarted;

  UPROPERTY(BlueprintAssignable, Category = "LandRoll")
  FOnLandRollFinished OnLandRollFinished;

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "LandRoll")
  bool LandRoll();

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "LandRoll|RPC")
  void Server_LandRoll();

  UFUNCTION(NetMulticast, Reliable, BlueprintCallable,
            Category = "LandRoll|RPC")
  void Multicast_PlayLandRollMontage();
};
