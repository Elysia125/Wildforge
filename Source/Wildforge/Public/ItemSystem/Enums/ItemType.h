#pragma once

#include "ItemType.generated.h"

UENUM(BlueprintType)
enum class EItemType : uint8 {
  Resource UMETA(DisplayName = "资源"),
  Equipable UMETA(DisplayName = "可装备物品"),
  Weapon UMETA(DisplayName = "武器"),
  Armor UMETA(DisplayName = "护甲"),
  Consumable UMETA(DisplayName = "消耗品"),
  Buildable UMETA(DisplayName = "可建造物品"),
  Other UMETA(DisplayName = "其他")
};