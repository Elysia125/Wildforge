// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "ItemSystem/Structs/ItemInfo.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "ItemDatabaseSubsystem.generated.h"

class UDataTable;

/**
 * 物品定义数据库（单一真相源）。
 *
 * 统一持有物品数据表 DT_Items，并在首次访问时构建「int32 ItemID -> 定义」的缓存，
 * 使 ItemID 查找为 O(1)，且对外不暴露 DataTable 的行名约定。
 * 服务器与客户端都可使用（客户端解析图标/名称）。
 */
UCLASS()
class WILDFORGE_API UItemDatabaseSubsystem : public UGameInstanceSubsystem {
  GENERATED_BODY()
public:
  virtual void Initialize(FSubsystemCollectionBase &Collection) override;

  /** 从任意世界上下文对象获取本子系统（失败返回 nullptr） */
  UFUNCTION(BlueprintPure, Category = "ItemDatabase",
            meta = (WorldContext = "WorldContextObject"))
  static UItemDatabaseSubsystem *Get(const UObject *WorldContextObject);

  /** 按 ItemID 查物品定义（走缓存，O(1)） */
  UFUNCTION(BlueprintPure, Category = "ItemDatabase")
  bool FindItem(int32 ItemID, FItemInformation &OutItem) const;

  /** C++ 版：返回缓存内定义的指针，未找到返回 nullptr（勿长期持有） */
  const FItemInformation *GetItemDefinition(int32 ItemID) const;

  /** 已解析的物品数据表（Initialize 时按项目设置加载） */
  UFUNCTION(BlueprintPure, Category = "ItemDatabase")
  UDataTable *GetItemTable() const { return ItemTable; }

private:
  // 运行时解析出的数据表（不复制、不序列化）
  UPROPERTY(Transient)
  TObjectPtr<UDataTable> ItemTable;

  // int32 ItemID -> 定义（懒构建，ItemTable 变化时重建）
  mutable TMap<int32, FItemInformation> ItemIndex;
  mutable const UDataTable *CachedTable = nullptr;

  void BuildIndex() const;
  void EnsureIndex() const;
};
