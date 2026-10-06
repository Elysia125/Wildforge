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

  // 移除槽位但不广播（供批量扣除在结尾统一广播）
  bool RemoveItemAtSlotInternal(int32 SlotIndex);

  // 从单个槽位扣除 Quantity 个（数量沿用 ItemQuality）。
  // Quantity < 0 表示清空整个槽位。返回实际扣除的数量；不广播。
  int32 RemoveSlotQuantityInternal(int32 SlotIndex, int32 Quantity);

  // 从某 ItemID 的所有槽位扣除 Quantity 个（跨槽，从最后一个槽位往前）。
  // Quantity < 0 表示全部扣除。返回实际扣除的数量；不广播。
  int32 RemoveByItemIDInternal(int32 ItemID, int32 Quantity);

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
  // 按物品定义 + 数量添加：优先堆叠进已有的同 ItemID 未满堆
  // （受 IsStackable / MaxStackSize 限制），溢出部分再放入空槽。
  // 返回实际加入的数量（背包满时可能小于 Quantity）。
  // 定义由调用方从 ItemDatabaseSubsystem 取好后传入，容器本身不持有数据表。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  int32 AddItemStack(const FItemInformation &Item, int32 Quantity);

  // 按 ItemID 添加（服务器权威）：内部经 ItemDatabaseSubsystem 查表取定义后
  // 走 AddItemStack。掉落/合成/奖励等服务器流程用它最方便，也避免传入空定义。
  // 返回实际加入的数量；ItemID 无效时返回 0。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  int32 AddItemByID(int32 ItemID, int32 Quantity);

  // 按【槽位】移除物品：从 SlotIndex 处的槽位扣除 Quantity 个，
  // -1（默认）表示清空整个槽位。部分扣除时保留该槽位（数量减少），
  // 扣空则释放该槽位。有任何物品被移除时返回 true。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool RemoveItem(int32 SlotIndex, int32 Quantity = -1);

  // 按【ItemID】移除物品：从该 ItemID 的所有槽位累计扣除 Quantity 个，
  // -1（默认）表示移除全部。从最后一个槽位往前逐格扣除，扣空的槽位被释放。
  // 有任何物品被移除时返回 true。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool RemoveAllItem(int32 ItemID, int32 Quantity = -1);

  // 整理容器：丢弃无效项(ItemID == -1)，合并同 ItemID 的堆叠
  // （是否可堆叠/最大堆叠数量决定每堆大小，满堆在前），
  // 按 ItemName 升序排序并紧凑排在前面，空槽集中到末尾
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  void OrganizeContainer();

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

  // 槽位间「放置」语义（拖拽落点用）：目标是空槽 -> 直接交换（等于移动）；
  // 目标是同 ItemID 的可堆叠物品、且未满堆 -> 把源数量并入目标（目标加满为止），
  // 装不下的多余数量留在源槽位；以上都不满足（不同 ItemID / 不可堆叠 / 目标已是
  // 满堆）-> 交换两槽。合并只在这两个槽位之间重分配数量，不重新堆叠到其它槽位。
  // 返回是否真的改动了容器。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool MoveOrMergeItem(int32 FromSlot, int32 ToSlot);

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  void ClearContainer();

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "ItemContainer")
  bool ResizeContainer(int32 NewCapacity);

  // ===== 客户端 -> 服务器：背包操作请求 =====
  // 声明在容器组件上，所有拥有容器的类（玩家、箱子…）自动继承，无需重复声明。
  // 约束：客户端只能对自己*拥有*的容器调用；共享容器（箱子等）需另行走
  // 玩家身上的交互 RPC + 服务器校验。
  // _Validate 只做廉价参数检查；真正的边界/占用检查在容器函数内部完成。

  // 添加物品：客户端只传 ItemID + 数量，服务器按 ItemID 查库取定义后堆叠
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_AddItem(int32 ItemID, int32 Quantity);

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_RemoveItem(int32 SlotIndex, int32 Quantity = -1);

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_RemoveAllItem(int32 ItemID, int32 Quantity = -1);

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_SwapSlots(int32 SlotA, int32 SlotB);

  // 客户端拖拽放置请求：服务器按同一规则做合并/交换（UI 不得自行改容器数据）
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_MoveOrMerge(int32 FromSlot, int32 ToSlot);

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_OrganizeContainer();

  // 清空背包（客户端可用，如“丢弃全部”）
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "ItemContainer|RPC")
  void Server_ClearContainer();

  // 注意：InitializeContainer / ResizeContainer 不收客户端 RPC，
  // 它们只保留为 BlueprintAuthorityOnly，由服务器/游戏流程调用。

  UFUNCTION(BlueprintPure, Category = "ItemContainer")
  EContainerType GetContainerType() const { return ContainerType; }
};
