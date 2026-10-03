// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "../BaseCharacter.h"
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

public:
  // Sets default values for this character's properties
  APlayerCharacter();

  UFUNCTION(BlueprintPure, Category = "Items", meta = (BlueprintThreadSafe))
  UPlayerInventory *GetInventory() const { return Inventory.Get(); }

  // ===== 客户端 -> 服务器：背包操作请求 =====
  // 仅由拥有该角色的客户端调用；服务器重新校验后再改权威数据。
  // 注意：不接受客户端提供的物品数据（FItemInformation），避免伪造属性。

  // 丢弃/消耗指定槽位的物品
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Items|RPC")
  void Server_RemoveItemAtSlot(int32 SlotIndex);

  // 按 ItemID 移除一件
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Items|RPC")
  void Server_RemoveItem(int32 ItemID);

  // 背包整理：交换两个槽位
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Items|RPC")
  void Server_SwapSlots(int32 SlotA, int32 SlotB);
};
