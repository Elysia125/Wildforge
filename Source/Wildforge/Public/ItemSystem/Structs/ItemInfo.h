#pragma once

#include "../Actors/ItemMaster.h"
#include "../Enums/ItemArmor.h"
#include "../Enums/ItemRarity.h"
#include "../Enums/ItemType.h"
#include "ItemInfo.generated.h"

USTRUCT(BlueprintType)
struct FItemInformation : public FTableRowBase {
  GENERATED_BODY()
  FItemInformation() = default;
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 ItemID;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FText ItemName;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  FText ItemDesc;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 ItemQuality;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 ItemDamage = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  bool IsStackable = true;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 MaxStackSize = 1;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 ItemCurHP = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 ItemMaxHP = 0;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  TObjectPtr<UTexture2D> ItemIcon;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  EItemType ItemType = EItemType::Other;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  EItemRarity ItemRarity = EItemRarity::Common;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  EItemArmor ItemArmor = EItemArmor::None;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  TSubclassOf<AItemMaster> ItemClass = nullptr;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  bool UseAmmo = false;

  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 Ammo = 0;
  
  UPROPERTY(EditAnywhere, BlueprintReadWrite)
  int32 AmmoMax = 0;
};