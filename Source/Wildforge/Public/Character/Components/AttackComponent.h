// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "structs/AttackMontageData.h"

#include "AttackComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttackFinished);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAttackStarted);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API UAttackComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  UAttackComponent();
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  bool bIsAttacking = false;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  float AttackCooldown = 0.5f;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  bool bCanAttack = true;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  float AttackDamage = 10.0f;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  float LastAttackTime = 0.0f;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  bool bCanCombo = false;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  int MontageSectionIndex = 0;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  int AttackMontageIndex = 0;
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  TArray<FAttackMontageData> AttackMontageList;

protected:
  // Called when the game starts
  virtual void BeginPlay() override;
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Attack")
  void PlayAttackMontage();
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Attack")
  void ResetAttackState();
  UFUNCTION()
  void OnAttackMontageEnded(UAnimMontage *Montage, bool bInterrupted);

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Attack")
  bool Attack();

  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Attack")
  void PerformDamageTrace();

  UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Attack")
  void PlayAttackEffects();
};
