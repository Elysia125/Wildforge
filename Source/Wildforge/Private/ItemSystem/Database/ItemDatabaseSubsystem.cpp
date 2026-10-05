// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Database/ItemDatabaseSubsystem.h"

#include "ItemSystem/Database/ItemSystemSettings.h"

#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UItemDatabaseSubsystem::Initialize(FSubsystemCollectionBase &Collection) {
  Super::Initialize(Collection);

  // 数据表由项目设置提供（Project Settings > Game > Item System）。
  const UItemSystemSettings *Settings = GetDefault<UItemSystemSettings>();
  if (!Settings || Settings->ItemTable.IsNull()) {
    UE_LOG(LogTemp, Warning,
           TEXT("ItemDatabaseSubsystem: 未配置 ItemTable（Project Settings > "
                "Game > Item System）"));
    return;
  }

  ItemTable = Settings->ItemTable.LoadSynchronous();
  if (!ItemTable) {
    UE_LOG(LogTemp, Warning, TEXT("ItemDatabaseSubsystem: 无法加载数据表 %s"),
           *Settings->ItemTable.ToString());
  }
}

UItemDatabaseSubsystem *
UItemDatabaseSubsystem::Get(const UObject *WorldContextObject) {
  if (!WorldContextObject || !GEngine) {
    return nullptr;
  }
  const UWorld *World = GEngine->GetWorldFromContextObject(
      WorldContextObject, EGetWorldErrorMode::ReturnNull);
  UGameInstance *GameInstance = World ? World->GetGameInstance() : nullptr;
  return GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>()
                      : nullptr;
}

void UItemDatabaseSubsystem::EnsureIndex() const {
  // ItemTable 被换过（或还没建）时重建缓存
  if (CachedTable != ItemTable.Get()) {
    BuildIndex();
  }
}

void UItemDatabaseSubsystem::BuildIndex() const {
  ItemIndex.Reset();
  CachedTable = ItemTable.Get();

  if (!ItemTable) {
    return;
  }

  if (ItemTable->GetRowStruct() != FItemInformation::StaticStruct()) {
    UE_LOG(LogTemp, Error,
           TEXT("ItemDatabaseSubsystem: 数据表 %s 的行结构不是 FItemInformation"),
           *ItemTable->GetName());
    return;
  }

  for (const TPair<FName, uint8 *> &Pair : ItemTable->GetRowMap()) {
    const FItemInformation *Row =
        reinterpret_cast<const FItemInformation *>(Pair.Value);
    if (!Row) {
      continue;
    }

    // 约定 1：行内的 ItemID 字段；约定 2：坑位为空时回退用行名的数字部分
    int32 Key = Row->ItemID;
    if (Key == 0) {
      Key = FCString::Atoi(*Pair.Key.ToString());
    }
    if (Key == 0) {
      continue;
    }

    // 同一 ItemID 重复时以先出现的为准
    if (!ItemIndex.Contains(Key)) {
      ItemIndex.Add(Key, *Row);
    }
  }
}

const FItemInformation *
UItemDatabaseSubsystem::GetItemDefinition(int32 ItemID) const {
  EnsureIndex();
  return ItemIndex.Find(ItemID);
}

bool UItemDatabaseSubsystem::FindItem(int32 ItemID,
                                      FItemInformation &OutItem) const {
  if (const FItemInformation *Found = GetItemDefinition(ItemID)) {
    OutItem = *Found;
    return true;
  }
  return false;
}
