#pragma once

#include "ItemRarity.generated.h"

UENUM(BlueprintType)
enum class EItemRarity : uint8 {
  Common UMETA(DisplayName = "普通"),
  Uncommon UMETA(DisplayName = "不常见"),
  Rare UMETA(DisplayName = "稀有"),
  Epic UMETA(DisplayName = "史诗"),
  Legendary UMETA(DisplayName = "传奇")
};