// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "ItemSystem/Enums/ContainerType.h"
#include "ItemSystem/Structs/ItemInfo.h"

#include "ItemContainer.generated.h"


// 容器内容发生任何变化时广播（服务端修改后、客户端 OnRep_Slots 后），供 UI 刷新
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnContainerChanged);

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

  // 容器类型
  UPROPERTY(BlueprintGetter = GetContainerType, Category = "ItemContainer")
  EContainerType ContainerType = EContainerType::PlayerStorage;

  // 由 Slots + SlotOccupied 重算 FreeSlots / ItemIDToSlot / UsedCount
  void RebuildDerivedState();

  UFUNCTION()
  void OnRep_Slots();

  // 广播 OnContainerChanged，通知订阅者（UI）刷新
  void NotifyContainerChanged();

  // 移除槽位但不广播（供 RemoveAllItem 批量调用后统一广播）
  bool RemoveItemAtSlotInternal(int32 SlotIndex);

public:
  // 容器内容变化时广播；Blueprint 也可绑定
  UPROPERTY(BlueprintAssignable, Category = "ItemContainer")
  FOnContainerChanged OnContainerChanged;

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
  
  void setContainerType(EContainerType InContainerType) { ContainerType = InContainerType; }
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

  // 整理容器：丢弃无效项(ItemID == -1)，合并同 ItemID 的堆叠
  // （是否可堆叠/最大堆叠数量决定每堆大小，满堆在前），
  // 按 ItemName 升序排序并紧凑排在前面，空槽集中到末尾
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  void OrganizeContainer();

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

  UFUNCTION(BlueprintPure, Category = "ItemContainer")
  EContainerType GetContainerType() const { return ContainerType; }
};
