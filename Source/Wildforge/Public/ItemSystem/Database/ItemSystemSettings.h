// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "ItemSystemSettings.generated.h"

class UDataTable;

/**
 * 物品系统的项目设置。
 *
 * 在 Project Settings > Game > Item System 中可编辑，序列化到
 * Config/DefaultGame.ini 的 [/Script/Wildforge.ItemSystemSettings] 段。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Item System"))
class WILDFORGE_API UItemSystemSettings : public UDeveloperSettings {
  GENERATED_BODY()
public:
  /** 物品定义数据表（DT_Items），服务器与客户端共用 */
  UPROPERTY(Config, EditAnywhere, Category = "Item Database",
            meta = (AllowedClasses = "/Script/Engine.DataTable"))
  TSoftObjectPtr<UDataTable> ItemTable;
};
