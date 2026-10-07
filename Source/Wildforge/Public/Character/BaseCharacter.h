// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Character/Components/CharacterAttributes.h"
#include "GameFramework/Character.h"

#include "BaseCharacter.generated.h"

/**
 * 所有角色的共同基类（玩家与将来的 AI 都从这里派生）。
 *
 * 目前它只负责两件**所有角色都存在**的事：
 *   1. `bReplicates = true`——不打开这一条，挂在上面的任何组件都不会复制；
 *   2. 持有 `UCharacterAttributes`（生命值等基础属性）。
 *
 * ## 能力组件挂在子类上，不要往这里塞
 *
 * 具体能力（攻击 / 翻滚 / 加速 / 闪现）都是独立的 `UActorComponent`，
 * 由**需要它的那个角色类**在构造函数里 `CreateDefaultSubobject` 创建：
 *
 *   - `UAttackComponent`、`ULandRollComponent`、`USprintBoostComponent`、
 *     `UBlinkComponent` → `APlayerCharacter`
 *
 * 之所以不放在本类：这些能力的**权威状态、复制通道、蒙太奇、输入入口**都是
 * 能力自己的事，基类只该提供「角色一定有」的东西。放在基类还会让每个派生类
 * 白白多背四个组件的开销与四条复制通道。
 *
 * ⚠️ 加速与闪现原先就是写在本类里的（`StartSpeedBoost` / `BlinkForward` 等），
 * 现已拆成 `USprintBoostComponent` / `UBlinkComponent`。
 * **旧蓝图节点（如 BP_ThirdPersonCharacter 里的 `Server_StartSpeedBoost`）需要
 * 手动重连到 `GetSprintBoostComponent()` / `GetBlinkComponent()` 上**，
 * 本类不再提供同名转发函数。
 */
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
