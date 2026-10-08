// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "BlinkComponentSettings.generated.h"

/**
 * 闪现组件（`UBlinkComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **闪现（Blink）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.BlinkComponentSettings]` 段。
 *
 * 覆盖语义：`bOverride_X` 不勾选 = 组件用自己的类默认值；勾选 = 用这里的值覆盖。
 *
 * ⚠️ **只有权威端读它**（`UBlinkComponent::ApplyGameplaySettingsOverrides`）：
 * 落点与距离上限必须只有一个决策者，客户端传多大的距离都会被服务器收敛。
 * 客户端的值来自组件上 `COND_InitialOnly` 的复制（含 `Cooldown`——它以前没有复制通道，
 * 客户端算冷却 UI 用的是自己那份类默认值，ini 一改就两端不一致）。
 *
 * 字段语义见 `UBlinkComponent` 上同名字段的注释。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "闪现（Blink）"))
class WILDFORGE_API UBlinkComponentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UBlinkComponentSettings *Get() {
    return GetDefault<UBlinkComponentSettings>();
  }

  /** 闪现允许的最大距离（cm）：服务器侧的上限，客户端传更大的值也只会被收敛到它。 */
  UPROPERTY(Config, EditAnywhere, Category = "Blink",
            meta = (InlineEditConditionToggle))
  bool bOverride_MaxDistance = false;

  UPROPERTY(Config, EditAnywhere, Category = "Blink",
            meta = (EditCondition = "bOverride_MaxDistance", ClampMin = "0", UIMin = "0",
                    ForceUnits = "cm"))
  float MaxDistance = 1200.0f;

  /** 两次闪现之间的最小间隔（秒），0 = 不限制。 */
  UPROPERTY(Config, EditAnywhere, Category = "Blink",
            meta = (InlineEditConditionToggle))
  bool bOverride_Cooldown = false;

  UPROPERTY(Config, EditAnywhere, Category = "Blink",
            meta = (EditCondition = "bOverride_Cooldown", ClampMin = "0", UIMin = "0",
                    ForceUnits = "s"))
  float Cooldown = 0.0f;

  /** 闪现后是否保留原有水平速度（true = 手感更连续；false = 落地急停）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Blink",
            meta = (InlineEditConditionToggle))
  bool bOverride_KeepVelocity = false;

  UPROPERTY(Config, EditAnywhere, Category = "Blink",
            meta = (EditCondition = "bOverride_KeepVelocity"))
  bool bKeepVelocity = false;

  /** 可选闪现蒙太奇的播放速率倍率（1 = 原速）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Blink|Anim",
            meta = (InlineEditConditionToggle))
  bool bOverride_MontagePlayRate = false;

  UPROPERTY(Config, EditAnywhere, Category = "Blink|Anim",
            meta = (EditCondition = "bOverride_MontagePlayRate", ClampMin = "0.01",
                    UIMin = "0.01"))
  float MontagePlayRate = 1.0f;
};
