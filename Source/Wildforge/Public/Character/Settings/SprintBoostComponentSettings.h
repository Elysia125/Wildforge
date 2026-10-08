// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "SprintBoostComponentSettings.generated.h"

/**
 * 加速组件（`USprintBoostComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **加速（Sprint Boost）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.SprintBoostComponentSettings]` 段。
 *
 * 覆盖语义：`bOverride_X` 不勾选 = 组件用自己的类默认值；勾选 = 用这里的值覆盖。
 *
 * ⚠️ **只有权威端读它**（`USprintBoostComponent::ApplyGameplaySettingsOverrides`）。
 * 客户端的值来自组件上 `COND_InitialOnly` 的复制——方向门控与蒙太奇播速在**每个端**
 * 都在跑，所以两端必须同源，不能让客户端读自己那份 ini。
 *
 * 字段语义见 `USprintBoostComponent` 上同名字段的注释。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "加速（Sprint Boost）"))
class WILDFORGE_API USprintBoostComponentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const USprintBoostComponentSettings *Get() {
    return GetDefault<USprintBoostComponentSettings>();
  }

  /** 从起始速度线性升到目标速度需要的总时间（秒）。
   *  只在 `StartSpeedBoost` 未显式传 Duration 时作为默认值（函数参数优先）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (InlineEditConditionToggle))
  bool bOverride_DefaultBoostDuration = false;

  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (EditCondition = "bOverride_DefaultBoostDuration", ClampMin = "0.01",
                    UIMin = "0.01", ForceUnits = "s"))
  float DefaultBoostDuration = 1.5f;

  /** 方向门控总开关：开启后只有向前移动才允许加速（侧移 / 倒退会被收掉）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (InlineEditConditionToggle))
  bool bOverride_SprintOnlyForward = false;

  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (EditCondition = "bOverride_SprintOnlyForward"))
  bool bSprintOnlyForward = true;

  /** 「算作向前」的最大夹角（度）：0 = 严格正前方（过于苛刻）；180 = 等于关掉方向判定。 */
  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (InlineEditConditionToggle))
  bool bOverride_MaxForwardSprintAngle = false;

  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (EditCondition = "bOverride_MaxForwardSprintAngle", ClampMin = "0",
                    ClampMax = "180", UIMin = "0", UIMax = "180"))
  float MaxForwardSprintAngle = 60.0f;

  /** 可选冲刺蒙太奇的播放速率倍率（1 = 原速）。蒙太奇在每个端本地播放，必须两端一致。 */
  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (InlineEditConditionToggle))
  bool bOverride_MontagePlayRate = false;

  UPROPERTY(Config, EditAnywhere, Category = "Sprint",
            meta = (EditCondition = "bOverride_MontagePlayRate", ClampMin = "0.01",
                    UIMin = "0.01"))
  float MontagePlayRate = 1.0f;
};
