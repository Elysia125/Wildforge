#pragma once

#include "ItemArmor.generated.h"

UENUM(BlueprintType)
enum class EItemArmor : uint8 {
  None UMETA(DisplayName = "无"),
  Helmet UMETA(DisplayName = "头盔"),
  Chest UMETA(DisplayName = "胸甲"),
  Gloves UMETA(DisplayName = "手套"),
  Pants UMETA(DisplayName = "护腿"),
  Boots UMETA(DisplayName = "靴子"),
};