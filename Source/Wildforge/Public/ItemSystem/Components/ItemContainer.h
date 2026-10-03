// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "ItemSystem/Structs/ItemInfo.h"

#include "ItemContainer.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
       BlueprintType, Blueprintable)
class WILDFORGE_API UItemContainer : public UActorComponent {
  GENERATED_BODY()
private:
  // 槽位存储：索引即槽位号（服务器权威，复制到客户端）
  UPROPERTY(ReplicatedUsing = OnRep_Slots)
  TArray<FItemInformation> Slots;

  // 每个槽位是否被占用，与 Slots 等长（复制，客户端重建派生数据时要用）
  UPROPERTY(Replicated)
  TArray<bool> SlotOccupied;

  // 以下为派生数据，不复制：客户端在 OnRep_Slots 里由 Slots + SlotOccupied 重建
  // 空闲槽位栈：AddItem 时 O(1) 弹出
  TArray<int32> FreeSlots;

  // ItemID -> 槽位索引，O(1) 查找
  // 如果同一 ItemID 允许存在多个槽位，改成 TMap<int32, TArray<int32>>
  TMap<int32, TArray<int32>> ItemIDToSlot;

  int32 UsedCount = 0;

  // 由 Slots + SlotOccupied 重算 FreeSlots / ItemIDToSlot / UsedCount
  void RebuildDerivedState();

  UFUNCTION()
  void OnRep_Slots();

public:
  // Sets default values for this component's properties
  UItemContainer();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  // 必须在 AddItem 之前调用一次
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  void InitializeContainer(int32 InCapacity);

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

  int32 FindEmptySlot() const;

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  // Index = INDEX_NONE 时自动找空槽；指定时放入指定槽
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool AddItem(const FItemInformation &Item, int32 Index = -1);

  // 按 ItemID 删除最后一个槽位（O(1)），如果同一 ItemID
  // 允许存在多个槽位，改成删除最后一个槽位
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool RemoveItem(int32 ItemID);
  // 按 ItemID 删除所有
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool RemoveAllItem(int32 ItemID);

  // 按槽位索引删除（O(1)）
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool RemoveItemAtSlot(int32 SlotIndex);

  // 以下均为只读查询：客户端本地读复制的数据，故用 BlueprintPure
  // O(1) 查找槽位索引，返回 INDEX_NONE 表示未找到
  UFUNCTION(BlueprintPure, Category = "ItemContainer",
            meta = (BlueprintThreadSafe))
  int32 FindItem(int32 ItemID) const;
  UFUNCTION(BlueprintPure, Category = "ItemContainer",
            meta = (BlueprintThreadSafe))
  TArray<int32> FindAllItems(int32 ItemID) const;

  UFUNCTION(BlueprintPure, Category = "ItemContainer",
            meta = (BlueprintThreadSafe))
  int32 GetItemCount() const { return UsedCount; }

  UFUNCTION(BlueprintPure, Category = "ItemContainer",
            meta = (BlueprintThreadSafe))
  int32 GetCapacity() const { return Slots.Num(); }

  UFUNCTION(BlueprintPure, Category = "ItemContainer",
            meta = (BlueprintThreadSafe))
  bool IsFull() const { return UsedCount >= Slots.Num(); }

  UFUNCTION(BlueprintPure, Category = "ItemContainer",
            meta = (BlueprintThreadSafe))
  bool IsSlotEmpty(int32 SlotIndex) const;

  UFUNCTION(BlueprintPure, Category = "ItemContainer")
  bool GetItemAtSlot(int32 SlotIndex, FItemInformation &OutItem) const;

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool SwapSlots(int32 SlotA, int32 SlotB);

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  void ClearContainer();

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool ResizeContainer(int32 NewCapacity);
};
