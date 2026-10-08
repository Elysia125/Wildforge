// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "LandRollComponentSettings.generated.h"

/**
 * 翻滚组件（`ULandRollComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **翻滚（Land Roll）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.LandRollComponentSettings]` 段。
 *
 * 覆盖语义：`bOverride_X` 不勾选 = 组件用自己的类默认值；勾选 = 用这里的值覆盖。
 *
 * ⚠️ **只有权威端读它**（`ULandRollComponent::ApplyGameplaySettingsOverrides`）。
 * 客户端的值来自组件上 `COND_InitialOnly` 的复制——蒙太奇播速与 BlendOut 解锁在
 * **每个端**的表现逻辑里都要读，只发 owner 会让旁观者用错值。
 *
 * 字段语义见 `ULandRollComponent` 上同名字段的注释。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "翻滚（Land Roll）"))
class WILDFORGE_API ULandRollComponentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const ULandRollComponentSettings *Get() {
    return GetDefault<ULandRollComponentSettings>();
  }

  /** 翻滚冷却（秒）：两次翻滚开始时间的最小间隔。0 = 不限制。
   *  （组件上这一项本来就是复制属性，客户端要拿它算冷却 UI 的进度。） */
  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (InlineEditConditionToggle))
  bool bOverride_Cooldown = false;

  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (EditCondition = "bOverride_Cooldown", ClampMin = "0", UIMin = "0",
                    ForceUnits = "s"))
  float Cooldown = 1.0f;

  /** 混合淡出（BlendOut）时就结束翻滚状态（默认开）：让移动在动画扫尾时就接上，
   *  而不是等 `OnMontageEnded` 再解锁。 */
  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (InlineEditConditionToggle))
  bool bOverride_FinishOnBlendOut = false;

  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (EditCondition = "bOverride_FinishOnBlendOut"))
  bool bFinishOnBlendOut = true;

  /** 翻滚开始时是否把速度清零（只在翻滚靠自己的位移曲线驱动时才需要打开）。 */
  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (InlineEditConditionToggle))
  bool bOverride_StopMovementOnStart = false;

  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (EditCondition = "bOverride_StopMovementOnStart"))
  bool bStopMovementOnStart = false;

  /** 翻滚蒙太奇的播放速率倍率（1 = 原速；组件默认 1.5 让翻滚显得利落）。 */
  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (InlineEditConditionToggle))
  bool bOverride_MontagePlayRate = false;

  UPROPERTY(Config, EditAnywhere, Category = "LandRoll",
            meta = (EditCondition = "bOverride_MontagePlayRate", ClampMin = "0.01",
                    UIMin = "0.01"))
  float MontagePlayRate = 1.5f;
};
