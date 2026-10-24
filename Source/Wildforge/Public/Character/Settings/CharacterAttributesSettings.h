// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "CharacterAttributesSettings.generated.h"

/**
 * 角色属性组件（`UCharacterAttributes`）的可调数值：初始等级、生命/精力上限、
 * 每级成长，以及升级经验曲线。
 *
 * 位置：Project Settings > Project > Game > **角色属性（Character Attributes）**；
 * 序列化到 `Config/DefaultGame.ini` 的
 * `[/Script/Wildforge.CharacterAttributesSettings]` 段。
 *
 * 覆盖语义：每一项都是「`bOverride_X` 开关 + `X` 值」一对。
 *   * **勾选**（本类默认全勾）= 权威端用这里的值初始化/驱动属性；
 *   * **不勾选** = 退回 `UCharacterAttributes` 的类默认值（蓝图子类可改）。
 *
 * ⚠️ **只有权威端读 ini**（`UCharacterAttributes::ApplyGameplaySettingsOverrides`
 * 在权威端 `BeginPlay` 调用）。客户端拿到的是组件上复制下来的
 * `MaxHealth` / `MaxEnergy` / `Level` / `ExperienceToNextLevel` 等生效值，
 * 以及 `COND_InitialOnly` 下发的成长参数（供 `GetExperienceRequiredForLevel`
 * 在客户端算同一份曲线）。
 *
 * ## 精力自然回复
 *
 *   - 每次消耗精力后等待 `EnergyRegenDelay` 秒，再以 `EnergyRegenPerSecond`
 *     的速度回复；加速持续扣费期间会不断刷新「最近消耗时间」，所以不会边扣边回。
 *   - 只有权威端推进回复，客户端只读 `Energy` 的复制值。
 *
 * ## 曲线公式（不按等级逐项配置，避免表越写越长）
 *
 *   - 等级 L 的生命上限 = `InitialHealth + HealthPerLevel * (L - 1)`
 *   - 等级 L 的精力上限 = `InitialEnergy + EnergyPerLevel * (L - 1)`
 *   - 从等级 L 升到 L+1 所需经验 =
 *       `round(BaseExperienceToNextLevel * ExperienceGrowthFactor ^ (L - 1))`
 *     即 1→2 级需要 Base 点，之后每级按 GrowthFactor 倍增长。
 *
 * 达到 `MaxLevel` 后不再获得经验，经验条显示为满。
 */
UCLASS(Config = Game, DefaultConfig,
       meta = (DisplayName = "角色属性（Character Attributes）"))
class WILDFORGE_API UCharacterAttributesSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UCharacterAttributesSettings *Get() {
    return GetDefault<UCharacterAttributesSettings>();
  }

  // ===== 等级 =====

  /** 角色出生时的等级（从 1 开始）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Level",
            meta = (InlineEditConditionToggle))
  bool bOverride_InitialLevel = true;

  UPROPERTY(Config, EditAnywhere, Category = "Level",
            meta = (EditCondition = "bOverride_InitialLevel", ClampMin = "1",
                    UIMin = "1"))
  int32 InitialLevel = 1;

  /** 等级上限。达到后不再获得经验。 */
  UPROPERTY(Config, EditAnywhere, Category = "Level",
            meta = (InlineEditConditionToggle))
  bool bOverride_MaxLevel = true;

  UPROPERTY(Config, EditAnywhere, Category = "Level",
            meta = (EditCondition = "bOverride_MaxLevel", ClampMin = "1",
                    UIMin = "1"))
  int32 MaxLevel = 100;

  // ===== 生命与精力 =====

  /** 1 级时的生命上限；出生时生命值直接补满到这个值。 */
  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (InlineEditConditionToggle))
  bool bOverride_InitialHealth = true;

  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (EditCondition = "bOverride_InitialHealth", ClampMin = "1",
                    UIMin = "1"))
  float InitialHealth = 100.0f;

  /** 1 级时的精力上限；出生时精力值直接补满到这个值。 */
  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (InlineEditConditionToggle))
  bool bOverride_InitialEnergy = true;

  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (EditCondition = "bOverride_InitialEnergy", ClampMin = "1",
                    UIMin = "1"))
  float InitialEnergy = 100.0f;

  /** 每升 1 级增加的生命上限。 */
  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (InlineEditConditionToggle))
  bool bOverride_HealthPerLevel = true;

  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (EditCondition = "bOverride_HealthPerLevel", ClampMin = "0",
                    UIMin = "0"))
  float HealthPerLevel = 10.0f;

  /** 每升 1 级增加的精力上限。 */
  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (InlineEditConditionToggle))
  bool bOverride_EnergyPerLevel = true;

  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (EditCondition = "bOverride_EnergyPerLevel", ClampMin = "0",
                    UIMin = "0"))
  float EnergyPerLevel = 10.0f;

  /** 精力自然回复速度（每秒）；0 = 不自动回复。 */
  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (InlineEditConditionToggle))
  bool bOverride_EnergyRegenPerSecond = true;

  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (EditCondition = "bOverride_EnergyRegenPerSecond",
                    ClampMin = "0", UIMin = "0"))
  float EnergyRegenPerSecond = 5.0f;

  /** 最后一次消耗精力后，延迟多久才开始回复（秒）。0 = 立即回复。 */
  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (InlineEditConditionToggle))
  bool bOverride_EnergyRegenDelay = true;

  UPROPERTY(Config, EditAnywhere, Category = "Vitals",
            meta = (EditCondition = "bOverride_EnergyRegenDelay", ClampMin = "0",
                    UIMin = "0"))
  float EnergyRegenDelay = 1.5f;

  // ===== 升级经验 =====

  /** 1 级升 2 级所需经验（公式里的 Base）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Experience",
            meta = (InlineEditConditionToggle))
  bool bOverride_BaseExperienceToNextLevel = true;

  UPROPERTY(Config, EditAnywhere, Category = "Experience",
            meta = (EditCondition = "bOverride_BaseExperienceToNextLevel",
                    ClampMin = "1", UIMin = "1"))
  int32 BaseExperienceToNextLevel = 100;

  /** 每高 1 级，升级所需经验的增长倍率（1.0 = 每级都只需要 Base 点）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Experience",
            meta = (InlineEditConditionToggle))
  bool bOverride_ExperienceGrowthFactor = true;

  UPROPERTY(Config, EditAnywhere, Category = "Experience",
            meta = (EditCondition = "bOverride_ExperienceGrowthFactor",
                    ClampMin = "1.0", UIMin = "1.0"))
  float ExperienceGrowthFactor = 1.15f;
};
