#pragma once

#include "ContainerType.generated.h"

UENUM(BlueprintType)
enum class EContainerType : uint8 {
  PlayerInventory UMETA(DisplayName = "玩家背包"),
  PlayerHotbar UMETA(DisplayName = "玩家快捷栏"),
  PlayerStorage UMETA(DisplayName = "玩家储物箱"),
  PlayerArmor UMETA(DisplayName = "玩家装备箱"),

  // 世界容器（宝箱 / 掉落箱 / 场景储物箱）。追加在末尾，不改变已有枚举值。
  WorldStorage UMETA(DisplayName = "世界容器"),
};