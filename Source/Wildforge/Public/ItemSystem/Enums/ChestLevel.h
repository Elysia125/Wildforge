#pragma once

#include "CoreMinimal.h"

#include "ChestLevel.generated.h"

/**
 * 宝箱等级（三档）。
 *
 * 只描述「这个箱子属于哪一档」，**不包含掉落规则**：具体掉落表、概率、
 * 数量由消费它的系统（宝箱 Actor / 战利品生成逻辑）按等级决定。
 * 作为 `AChestActor::ChestLevel` 复制到各端，客户端可据此显示
 * 不同颜色 / 图标 / 开箱特效。
 *
 * ⚠️ 等级属于**宝箱 Actor**，不属于 `UStorageContainer`：后者是所有世界容器
 * （储物箱 / 掉落箱 / 尸体容器）共用的存储组件，不应被宝箱专用规则污染。
 *
 * ⚠️ 枚举值会进入网络复制与存档：新增档位请追加在末尾，不要重排或复用
 * 已有数值。
 */
UENUM(BlueprintType)
enum class EChestLevel : uint8 {
  /** 普通宝箱：最常见，基础掉落。 */
  Common UMETA(DisplayName = "普通"),

  /** 稀有宝箱：中级掉落，通常需要钥匙或解谜。 */
  Rare UMETA(DisplayName = "稀有"),

  /** 传奇宝箱：最高档，稀有掉落 / 首领奖励。 */
  Legendary UMETA(DisplayName = "传奇"),
};
