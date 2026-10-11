#pragma once

#include "CoreMinimal.h"

#include "ItemSystem/Structs/ItemInfo.h"

#include "QuickSlotContext.generated.h"

class UInventorySlotWidget;

USTRUCT(BlueprintType)
struct FQuickSlotContext
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite)
    FItemInformation Item;

    UPROPERTY(BlueprintReadWrite)
    UInventorySlotWidget* SlotWidget = nullptr;
};