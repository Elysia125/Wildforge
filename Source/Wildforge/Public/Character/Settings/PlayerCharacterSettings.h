// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "PlayerCharacterSettings.generated.h"

/**
 * 玩家角色（`APlayerCharacter`）自身的可调数值——**不是**某个能力组件的参数，
 * 所以不放在 `Character/Settings/` 下的那几个能力配置类里。
 *
 * 位置：Project Settings > Project > Game > **玩家角色（Player Character）**；
 * 序列化到 `Config/DefaultGame.ini` 的
 * `[/Script/Wildforge.PlayerCharacterSettings]` 段。
 *
 * 覆盖语义：`bOverride_X` 不勾选 = 角色用自己的类默认值（也就是蓝图子类
 * `BP_ThirdPersonCharacter` 的类默认值）；勾选 = 用这里的值覆盖。
 *
 * ⚠️ **只有权威端读它**：客户端的背包容器容量来自复制过来的 `Slots` 数组
 * （`OnRep_Slots` → `RebuildDerivedState`），服务器说了算，不需要、也不应该下发这个数值。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "玩家角色（Player Character）"))
class WILDFORGE_API UPlayerCharacterSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UPlayerCharacterSettings *Get() {
    return GetDefault<UPlayerCharacterSettings>();
  }

  /** 背包容量（格数），见类注释。
   *  ⚠️ 必须 > 0：0 格容器会让所有 `AddItem` 都失败。 */
  UPROPERTY(Config, EditAnywhere, Category = "PlayerCharacter",
            meta = (InlineEditConditionToggle))
  bool bOverride_InventoryCapacity = false;

  UPROPERTY(Config, EditAnywhere, Category = "PlayerCharacter",
            meta = (EditCondition = "bOverride_InventoryCapacity", ClampMin = "1",
                    UIMin = "1"))
  int32 InventoryCapacity = 30;
};
