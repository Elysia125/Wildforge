// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"

#include "CharacterAttributes.generated.h"

// 属性变化通知：服务器权威改动 与 客户端 RepNotify 都会广播，UI/蓝图可绑定
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMaxHealthChanged, float,
                                            NewMaxHealth);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
       BlueprintType, Blueprintable)
class WILDFORGE_API UCharacterAttributes : public UActorComponent {
  GENERATED_BODY()
private:
  UPROPERTY(ReplicatedUsing = OnRep_MaxHealth, BlueprintGetter = GetMaxHealth,
            BlueprintSetter = SetMaxHealth, Category = "Attributes")
  float MaxHealth = 100.0f;

  UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintGetter = GetHealth,
            BlueprintSetter = SetHealth, Category = "Attributes")
  float Health = 100.0f;

  // 属性变化时广播；服务器由 Set 函数调用，客户端由 OnRep 调用
  void NotifyHealthChanged();
  void NotifyMaxHealthChanged();

public:
  // Sets default values for this component's properties
  UCharacterAttributes();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  // UI / 蓝图绑定这些事件即可在对应属性变化时刷新
  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnHealthChanged OnHealthChanged;

  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnMaxHealthChanged OnMaxHealthChanged;

private:
  UFUNCTION()
  void OnRep_Health();

  UFUNCTION()
  void OnRep_MaxHealth();

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

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetHealth(float InHealth) {
    if (Health != InHealth) {
      Health = InHealth;
      NotifyHealthChanged();
    }
  }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void AddHealth(float InHealth) { SetHealth(Health + InHealth); }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetMaxHealth() const { return MaxHealth; }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetMaxHealth(float InHealth) {
    if (MaxHealth != InHealth) {
      MaxHealth = InHealth;
      NotifyMaxHealthChanged();
    }
  }
};
