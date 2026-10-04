// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Components/PlayerInventory.h"

UPlayerInventory::UPlayerInventory() : UItemContainer() {
  setContainerType(EContainerType::PlayerInventory);
}
