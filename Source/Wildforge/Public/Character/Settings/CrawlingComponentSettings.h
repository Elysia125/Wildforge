// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "CrawlingComponentSettings.generated.h"

/**
 * 趴下（爬行）组件（`UCrawlingComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **趴下（Crawl）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.CrawlingComponentSettings]` 段。
 *
 * 覆盖语义：`bOverride_X` 不勾选 = 组件用自己的类默认值；勾选 = 用这里的值覆盖。
 *
 * ⚠️ **只有权威端读它**（`UCrawlingComponent::ApplyGameplaySettingsOverrides`）。
 * 客户端的值来自组件上 `COND_InitialOnly` 的复制——过渡判定的阈值与蒙太奇播速在
 * **每个端**的表现逻辑里都要读。
 *
 * ⚠️ `MaxWalkSpeed`（趴下速度）**必须 > 0**：0 是「角色不能动」的合法速度值，
 *    写进去会把角色永久钉在原地（bug-027）。
 *
 * 字段语义见 `UCrawlingComponent` 上同名字段的注释。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "趴下（Crawl）"))
class WILDFORGE_API UCrawlingComponentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UCrawlingComponentSettings *Get() {
    return GetDefault<UCrawlingComponentSettings>();
  }

  /** 趴下时的爬行最大速度（cm/s）：进入趴下时写入、起立结束时精确还原。
   *  ⚠️ 必须 > 0，理由见类注释（bug-027）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (InlineEditConditionToggle))
  bool bOverride_ProneMaxWalkSpeed = false;

  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (EditCondition = "bOverride_ProneMaxWalkSpeed", ClampMin = "1",
                    UIMin = "1", ForceUnits = "cm/s"))
  float ProneMaxWalkSpeed = 150.0f;

  /** 判定「奔跑中趴下」的水平速度阈值（cm/s）：只有配了奔跑趴下蒙太奇才参与判定。 */
  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (InlineEditConditionToggle))
  bool bOverride_RunTransitionSpeedThreshold = false;

  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (EditCondition = "bOverride_RunTransitionSpeedThreshold", ClampMin = "0",
                    UIMin = "0", ForceUnits = "cm/s"))
  float RunTransitionSpeedThreshold = 300.0f;

  /** 两次过渡动作之间的最小间隔（秒），0 = 不限制。 */
  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (InlineEditConditionToggle))
  bool bOverride_Cooldown = false;

  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (EditCondition = "bOverride_Cooldown", ClampMin = "0", UIMin = "0",
                    ForceUnits = "s"))
  float Cooldown = 0.5f;

  /** 过渡动作在混合淡出（BlendOut）时就收尾（默认开）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (InlineEditConditionToggle))
  bool bOverride_FinishOnBlendOut = false;

  UPROPERTY(Config, EditAnywhere, Category = "Crawl",
            meta = (EditCondition = "bOverride_FinishOnBlendOut"))
  bool bFinishOnBlendOut = true;

  /** 三个过渡蒙太奇的播放速率倍率（1 = 原速）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Crawl|Anim",
            meta = (InlineEditConditionToggle))
  bool bOverride_MontagePlayRate = false;

  UPROPERTY(Config, EditAnywhere, Category = "Crawl|Anim",
            meta = (EditCondition = "bOverride_MontagePlayRate", ClampMin = "0.01",
                    UIMin = "0.01"))
  float MontagePlayRate = 1.0f;
};
