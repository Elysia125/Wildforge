// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "AttackComponentSettings.generated.h"

/**
 * 攻击组件（`UAttackComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **攻击（Attack）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.AttackComponentSettings]` 段，
 * 打包后的服务器可以在 `Saved/Config/<Platform>/Game.ini` 的同名段里覆盖（改完重启即可，
 * 不用重新编译、也不用重新打包）。
 *
 * 覆盖语义：每一项都是「`bOverride_X` 开关 + `X` 值」一对——
 *   * **不勾选** = 组件保留自己那份类默认值（本类里的数值只是初始建议值，不参与应用）；
 *   * **勾选** = 权威端用这里的值覆盖组件的同名字段。
 *
 * ⚠️ **只有权威端读它**（`UAttackComponent::ApplyGameplaySettingsOverrides` 在权威端
 * `BeginPlay` 调用）：客户端本地那份 ini 是玩家可改的文件，拿它驱动玩法就等于让客户端
 * 说了算。客户端认的是组件上 `COND_InitialOnly` 复制下来的那份值。
 *
 * 字段语义与每个下限的来由，见 `UAttackComponent` 上同名字段的注释。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "攻击（Attack）"))
class WILDFORGE_API UAttackComponentSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UAttackComponentSettings *Get() {
    return GetDefault<UAttackComponentSettings>();
  }

  // ===== 冷却与连击窗口 =====

  /** 两次「起手」之间的最小间隔（秒）。连击不受它约束（那是 ComboMinInterval 的活）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_Cooldown = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_Cooldown", ClampMin = "0", UIMin = "0",
                    ForceUnits = "s"))
  float Cooldown = 0.5f;

  /** 连击接招的最小间隔（秒）：只用来防「被篡改的客户端在同一个窗口里刷包」。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_ComboMinInterval = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_ComboMinInterval", ClampMin = "0",
                    UIMin = "0", ForceUnits = "s"))
  float ComboMinInterval = 0.15f;

  /** 连击切段交叉淡入时长的下限（秒）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_ComboBlendInMinTime = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_ComboBlendInMinTime", ClampMin = "0",
                    UIMin = "0", ForceUnits = "s"))
  float ComboBlendInMinTime = 0.05f;

  /** 连击切段交叉淡入时长的上限（秒）：窗口靠后时防止淡入过长、动作发肉。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_ComboBlendInMaxTime = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_ComboBlendInMaxTime", ClampMin = "0",
                    UIMin = "0", ForceUnits = "s"))
  float ComboBlendInMaxTime = 0.2f;

  // ===== 伤害与检测（服务器权威：伤害判定只在服务器跑）=====

  /** 单次攻击的伤害值。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_Damage = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_Damage", ClampMin = "0", UIMin = "0"))
  float Damage = 10.0f;

  /** 伤害检测射程（cm）：从角色胸口沿朝向往前扫这么远。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_Range = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_Range", ClampMin = "0", UIMin = "0",
                    ForceUnits = "cm"))
  float Range = 200.0f;

  /** 伤害检测半径（cm）：0 = 细线段（近战手感更精准）；> 0 = 球形扫掠。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_TraceRadius = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_TraceRadius", ClampMin = "0", UIMin = "0",
                    ForceUnits = "cm"))
  float TraceRadius = 0.0f;

  /** 检测起点相对角色原点的 Z 偏移（cm）：原点在胶囊体中心，胸口约 +30（**可以为负**）。 */
  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (InlineEditConditionToggle))
  bool bOverride_TraceHeightOffset = false;

  UPROPERTY(Config, EditAnywhere, Category = "Attack",
            meta = (EditCondition = "bOverride_TraceHeightOffset", ForceUnits = "cm"))
  float TraceHeightOffset = 30.0f;
};
