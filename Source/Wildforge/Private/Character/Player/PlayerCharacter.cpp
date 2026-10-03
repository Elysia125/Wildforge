// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Player/PlayerCharacter.h"

APlayerCharacter::APlayerCharacter() : ABaseCharacter() {
  Inventory = CreateDefaultSubobject<UPlayerInventory>(TEXT("Inventory"));
  Inventory->InitializeContainer(10);
}

// ===== 客户端 -> 服务器：背包操作请求 =====
// _Validate 只做廉价的参数合法性判断；真正的边界/占用检查在容器函数内部完成。
// 越界等非法请求直接忽略（容器返回 false），不在此处断开连接。

bool APlayerCharacter::Server_RemoveItemAtSlot_Validate(int32 SlotIndex) {
  return SlotIndex >= 0;
}

void APlayerCharacter::Server_RemoveItemAtSlot_Implementation(
    int32 SlotIndex) {
  if (Inventory) {
    Inventory->RemoveItemAtSlot(SlotIndex);
  }
}

bool APlayerCharacter::Server_RemoveItem_Validate(int32 ItemID) {
  return ItemID >= 0;
}

void APlayerCharacter::Server_RemoveItem_Implementation(int32 ItemID) {
  if (Inventory) {
    Inventory->RemoveItem(ItemID);
  }
}

bool APlayerCharacter::Server_SwapSlots_Validate(int32 SlotA, int32 SlotB) {
  return SlotA >= 0 && SlotB >= 0;
}

void APlayerCharacter::Server_SwapSlots_Implementation(int32 SlotA,
                                                       int32 SlotB) {
  if (Inventory) {
    Inventory->SwapSlots(SlotA, SlotB);
  }
}