#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "ChestSettings.generated.h"

/**
 * 宝箱（`AChestActor`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **宝箱（Chest）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.ChestSettings]` 段。
 *
 * 覆盖语义（与 `UPlayerCharacterSettings` 等配置类一致）：
 *   - `bOverride_X` 不勾选 = 宝箱 Actor 用自己的类默认值
 *     （`AChestActor::Capacity` / `LootCountMin` / `LootCountMax` /
 *     `LootStackMin` / `LootStackMax`）；
 *   - 勾选 = 用这里的值覆盖，服务器在 `AChestActor::BeginPlay` 读取并夹紧。
 *
 * ⚠️ **只有权威端读它**：客户端不拿这些值驱动玩法。容器容量由复制下来的
 * `Slots` 推导，战利品内容由服务器生成后复制；客户端的本地 ini 是玩家可改
 * 的文件，不能作为权威来源。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "宝箱（Chest）"))
class WILDFORGE_API UChestSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UChestSettings *Get() { return GetDefault<UChestSettings>(); }

  /** 是否用 ini 的值覆盖宝箱容量。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (InlineEditConditionToggle))
  bool bOverride_Capacity = false;

  /**
   * 宝箱容器的格数（必须 > 0）。
   *
   * 每个宝箱 Actor 都可以在类默认值里单独改；这里的值用于统一覆盖
   * （例如把所有宝箱统一扩到 20 格）。
   */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (EditCondition = "bOverride_Capacity", ClampMin = "1",
                    UIMin = "1"))
  int32 Capacity = 12;

  /** 是否用 ini 的值覆盖「每次生成战利品的数量下限」。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (InlineEditConditionToggle))
  bool bOverride_LootCountMin = false;

  /** 宝箱打开前随机生成的最少物品件数（0 = 允许空箱）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (EditCondition = "bOverride_LootCountMin", ClampMin = "0",
                    UIMin = "0"))
  int32 LootCountMin = 2;

  /** 是否用 ini 的值覆盖「每次生成战利品的数量上限」。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (InlineEditConditionToggle))
  bool bOverride_LootCountMax = false;

  /** 宝箱打开前随机生成的最多物品件数（小于下限时会被抬到下限）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (EditCondition = "bOverride_LootCountMax", ClampMin = "0",
                    UIMin = "0"))
  int32 LootCountMax = 4;

  /** 是否用 ini 的值覆盖「每堆战利品数量下限」。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (InlineEditConditionToggle))
  bool bOverride_LootStackMin = false;

  /**
   * 每堆战利品随机数量的下限（按物品 `MaxStackSize` 夹紧；
   * 不可堆叠物品恒为 1）。
   */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (EditCondition = "bOverride_LootStackMin", ClampMin = "1",
                    UIMin = "1"))
  int32 LootStackMin = 1;

  /** 是否用 ini 的值覆盖「每堆战利品数量上限」。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (InlineEditConditionToggle))
  bool bOverride_LootStackMax = false;

  /** 每堆战利品随机数量的上限（小于下限时会被抬到下限，且不超过物品 MaxStackSize）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Chest",
            meta = (EditCondition = "bOverride_LootStackMax", ClampMin = "1",
                    UIMin = "1"))
  int32 LootStackMax = 3;
};
