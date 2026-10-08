// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "SlideComponentSettings.generated.h"

/**
 * 滑行组件（`USlideComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **滑行（Slide）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.SlideComponentSettings]` 段。
 *
 * 覆盖语义：`bOverride_X` 不勾选 = 组件用自己的类默认值；勾选 = 用这里的值覆盖。
 *
 * ⚠️ **只有权威端读它**（`USlideComponent::ApplyGameplaySettingsOverrides`）。
 * 滑行是「两端各跑同一套速度曲线」的能力（客户端预测，见 bug-029），所以这里的每一项
 * 都必须靠 `COND_InitialOnly` 复制下发——客户端自己读 ini 会让两端曲线分叉、出现橡皮筋。
 *
 * 字段语义见 `USlideComponent` 上同名字段的注释。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "滑行（Slide）"))
class WILDFORGE_API USlideComponentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const USlideComponentSettings *Get() {
    return GetDefault<USlideComponentSettings>();
  }

  // ===== 速度曲线 =====

  /** 滑行时长（秒）：速度曲线在这段时间内从起始速度衰减到 EndSpeed。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (InlineEditConditionToggle))
  bool bOverride_Duration = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (EditCondition = "bOverride_Duration", ClampMin = "0.01",
                    UIMin = "0.01", ForceUnits = "s"))
  float Duration = 1.0f;

  /** 速度曲线左端下限（cm/s）：起手取 max(当前水平速度, 它)，所以冲刺中起滑不会被减速。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (InlineEditConditionToggle))
  bool bOverride_StartSpeed = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (EditCondition = "bOverride_StartSpeed", ClampMin = "0", UIMin = "0",
                    ForceUnits = "cm/s"))
  float StartSpeed = 900.0f;

  /** 速度曲线右端（cm/s）：Duration 走完时的目标速度。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (InlineEditConditionToggle))
  bool bOverride_EndSpeed = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (EditCondition = "bOverride_EndSpeed", ClampMin = "0", UIMin = "0",
                    ForceUnits = "cm/s"))
  float EndSpeed = 300.0f;

  /** 速度被夹到曲线上的最大下降速率（cm/s²）：0 = 不限制（立刻夹到曲线值）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (InlineEditConditionToggle))
  bool bOverride_MaxSpeedDecelRate = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Speed",
            meta = (EditCondition = "bOverride_MaxSpeedDecelRate", ClampMin = "0",
                    UIMin = "0"))
  float MaxSpeedDecelRate = 1500.0f;

  // ===== 起滑 / 结束判定 =====

  /** 起滑速度要求（cm/s）：当前水平速度低于它就拒绝本次滑行。0 = 站着也能滑。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (InlineEditConditionToggle))
  bool bOverride_MinSpeedToStartSlide = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (EditCondition = "bOverride_MinSpeedToStartSlide", ClampMin = "0",
                    UIMin = "0", ForceUnits = "cm/s"))
  float MinSpeedToStartSlide = 0.0f;

  /** 低于这个水平速度就提前结束滑行（撞墙 / 上坡 / 被夹住）。0 = 不检查。
   *  ⚠️ 它要小于 EndSpeed，否则滑行会在中途就被这条规则截断。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (InlineEditConditionToggle))
  bool bOverride_MinSpeedToContinue = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (EditCondition = "bOverride_MinSpeedToContinue", ClampMin = "0",
                    UIMin = "0", ForceUnits = "cm/s"))
  float MinSpeedToContinue = 150.0f;

  /** 速度检查的宽限时间（秒）：起滑后这段时间内不看最低速度。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (InlineEditConditionToggle))
  bool bOverride_MinSpeedGraceTime = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (EditCondition = "bOverride_MinSpeedGraceTime", ClampMin = "0",
                    UIMin = "0", ForceUnits = "s"))
  float MinSpeedGraceTime = 0.15f;

  /** 离地（跳跃 / 掉下平台）就结束滑行（位移模型「贴地滑」只在地面成立）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (InlineEditConditionToggle))
  bool bOverride_EndSlideWhenAirborne = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Rules",
            meta = (EditCondition = "bOverride_EndSlideWhenAirborne"))
  bool bEndSlideWhenAirborne = true;

  // ===== 转向与摩擦 =====

  /** 滑行期间是否允许有限转向（关掉 = 纯直线滑行，MaxAcceleration 置 0）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (InlineEditConditionToggle))
  bool bOverride_AllowSteering = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (EditCondition = "bOverride_AllowSteering"))
  bool bAllowSteering = true;

  /** 滑行期间的 MaxAcceleration：转向的唯一强度来源（不是速度上限）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (InlineEditConditionToggle))
  bool bOverride_MaxAcceleration = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (EditCondition = "bOverride_MaxAcceleration", ClampMin = "0",
                    UIMin = "0"))
  float MaxAcceleration = 300.0f;

  /** 滑行期间的地面摩擦（默认 0 = 减速只由速度曲线负责）。
   *  ⚠️ 它与 BrakingDeceleration 同时为 0 时引擎才会跳过刹车计算。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (InlineEditConditionToggle))
  bool bOverride_GroundFriction = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (EditCondition = "bOverride_GroundFriction", ClampMin = "0",
                    UIMin = "0"))
  float GroundFriction = 0.0f;

  /** 滑行期间的步行刹车减速度（默认 0，理由同上）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (InlineEditConditionToggle))
  bool bOverride_BrakingDeceleration = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Steering",
            meta = (EditCondition = "bOverride_BrakingDeceleration", ClampMin = "0",
                    UIMin = "0"))
  float BrakingDeceleration = 0.0f;

  // ===== 表现 =====

  /** 滑行蒙太奇的播放速率倍率（1 = 原速）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Anim",
            meta = (InlineEditConditionToggle))
  bool bOverride_MontagePlayRate = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Anim",
            meta = (EditCondition = "bOverride_MontagePlayRate", ClampMin = "0.01",
                    UIMin = "0.01"))
  float MontagePlayRate = 1.0f;

  /** 收尾时停掉蒙太奇的淡出时长（秒）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Slide|Anim",
            meta = (InlineEditConditionToggle))
  bool bOverride_MontageStopBlendOutTime = false;

  UPROPERTY(Config, EditAnywhere, Category = "Slide|Anim",
            meta = (EditCondition = "bOverride_MontageStopBlendOutTime", ClampMin = "0",
                    UIMin = "0", ForceUnits = "s"))
  float MontageStopBlendOutTime = 0.15f;
};
