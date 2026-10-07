// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "../BaseCharacter.h"
#include "Character/Components/AttackComponent.h"
#include "Character/Components/LandRollComponent.h"
#include "ItemSystem/Components/PlayerInventory.h"

#include "PlayerCharacter.generated.h"

/**
 * 玩家角色
 *
 * 攻击期间的移动门控在这里落地：订阅攻击组件的
 * `OnAttackStarted`（禁止移动）/ `OnAttackFinished`（恢复移动）两个通知，
 * 不对攻击组件的内部逻辑做任何假设——组件只负责广播「开始了 / 结束了」。
 */
UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable)
class WILDFORGE_API APlayerCharacter : public ABaseCharacter {
  GENERATED_BODY()
private:
  UPROPERTY(BlueprintGetter = GetInventory, Category = "Items")
  TObjectPtr<UPlayerInventory> Inventory;

  // 攻击组件（由构造函数创建，随宿主 Actor 复制）
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Attack",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<UAttackComponent> AttackComponent;
  // 翻滚组件（由构造函数创建，随宿主 Actor 复制）
  UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "LandRoll",
            meta = (AllowPrivateAccess = "true"))
  TObjectPtr<ULandRollComponent> LandRollComponent;
  // 禁止移动前的移动模式，攻击结束后用来恢复（对应「启动移动」操作）。
  // 只在本机本地读写，不复制：EndAttack 的判断依据是 CharacterMovement
  // 自己复制的移动模式，所以两端各自维护一份本地缓存即可。
  TEnumAsByte<EMovementMode> MovementModeBeforeAttack = MOVE_Walking;

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;

  // 攻击开始：禁止移动
  UFUNCTION()
  void HandleAttackStarted();

  // 攻击结束：恢复移动
  UFUNCTION()
  void HandleAttackFinished();

public:
  // Sets default values for this character's properties
  APlayerCharacter();

  UFUNCTION(BlueprintPure, Category = "Items", meta = (BlueprintThreadSafe))
  UPlayerInventory *GetInventory() const { return Inventory.Get(); }

  UFUNCTION(BlueprintPure, Category = "Attack", meta = (BlueprintThreadSafe))
  UAttackComponent *GetAttackComponent() const { return AttackComponent.Get(); }
};
