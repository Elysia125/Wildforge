// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Components/ItemContainer.h"

#include "Net/UnrealNetwork.h"

// Sets default values for this component's properties
UItemContainer::UItemContainer() {
  // Set this component to be initialized when the game starts, and to be ticked
  // every frame.  You can turn these features off to improve performance if you
  // don't need them.
  PrimaryComponentTick.bCanEverTick = false;

  // 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）
  SetIsReplicatedByDefault(true);
}

void UItemContainer::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 仅复制给拥有者（玩家背包隐私）。通用容器（箱子等）如需要额外可见性，
  // 在子类重写本函数并按需调整条件。
  DOREPLIFETIME_CONDITION(UItemContainer, Slots, COND_OwnerOnly);
  DOREPLIFETIME_CONDITION(UItemContainer, SlotOccupied, COND_OwnerOnly);
}

void UItemContainer::OnRep_Slots() { RebuildDerivedState(); }

void UItemContainer::RebuildDerivedState() {
  // 派生数据全部可由 Slots + SlotOccupied 重算，客户端只做这件事
  const int32 Capacity = Slots.Num();

  FreeSlots.Reset(Capacity);
  FreeSlots.Reserve(Capacity);
  ItemIDToSlot.Empty(Capacity);
  ItemIDToSlot.Reserve(Capacity);
  UsedCount = 0;

  // 倒序遍历，使 FreeSlots 弹出顺序与服务器 InitializeContainer 一致（先弹下标 0）
  for (int32 i = Capacity - 1; i >= 0; --i) {
    // SlotOccupied 与 Slots 同批复制，正常等长；不齐时按空槽处理，等补齐后重算幂等
    if (i < SlotOccupied.Num() && SlotOccupied[i]) {
      ++UsedCount;
      ItemIDToSlot.FindOrAdd(Slots[i].ItemID).Add(i);
    } else {
      FreeSlots.Add(i);
    }
  }
}

// Called when the game starts
void UItemContainer::BeginPlay() {
  Super::BeginPlay();

  // ...
}

// Called every frame
void UItemContainer::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // ...
}

void UItemContainer::InitializeContainer(int32 InCapacity) {
  if (InCapacity <= 0)
    return;

  Slots.SetNum(InCapacity);
  SlotOccupied.SetNum(InCapacity);

  FreeSlots.Reset(InCapacity);
  FreeSlots.Reserve(InCapacity);
  // 倒序压入，弹出时从 0 开始，符合直觉
  for (int32 i = InCapacity - 1; i >= 0; --i) {
    SlotOccupied[i] = false;
    FreeSlots.Add(i);
  }

  ItemIDToSlot.Empty(InCapacity);
  ItemIDToSlot.Reserve(InCapacity);
  UsedCount = 0;
}

bool UItemContainer::AddItem(const FItemInformation &Item, int32 Index) {
  int32 TargetSlot = INDEX_NONE;

  if (Index == INDEX_NONE) {
    if (FreeSlots.Num() == 0) {
      return false; // 满了
    }
    TargetSlot = FreeSlots.Pop(EAllowShrinking::No);
  } else {
    if (!Slots.IsValidIndex(Index)) {
      return false;
    }
    if (SlotOccupied[Index]) {
      return false;
    }
    TargetSlot = Index;
    // 显式指定槽位时，从 FreeSlots 里移除（O(n)，但仅此路径）
    FreeSlots.RemoveSingleSwap(TargetSlot, EAllowShrinking::No);
  }

  Slots[TargetSlot] = Item;
  SlotOccupied[TargetSlot] = true;
  if (ItemIDToSlot.Contains(Item.ItemID)) {
    ItemIDToSlot[Item.ItemID].Add(TargetSlot);
  } else {
    ItemIDToSlot.Add(Item.ItemID, {TargetSlot});
  }
  ++UsedCount;
  return true;
}

bool UItemContainer::RemoveItem(int32 ItemID) {
  if (const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID)) {
    return RemoveItemAtSlot(FoundSlots->Last()); // 删除最后一个槽位
  }
  return false;
}

bool UItemContainer::RemoveAllItem(int32 ItemID) {
  if (const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID)) {
    for (int32 SlotIndex : *FoundSlots) {
      RemoveItemAtSlot(SlotIndex);
    }
    return true;
  }
  return false;
}

bool UItemContainer::RemoveItemAtSlot(int32 SlotIndex) {
  if (!Slots.IsValidIndex(SlotIndex))
    return false;
  if (!SlotOccupied[SlotIndex])
    return false;

  const int32 ItemID = Slots[SlotIndex].ItemID;
  ItemIDToSlot.FindChecked(ItemID).Remove(SlotIndex);
  if (ItemIDToSlot.FindChecked(ItemID).Num() == 0) {
    ItemIDToSlot.Remove(ItemID);
  }

  // 清空引用，避免持有 UObject/TSoftObjectPtr 造成额外内存占用
  Slots[SlotIndex] = FItemInformation();
  SlotOccupied[SlotIndex] = false;

  FreeSlots.Add(SlotIndex);
  --UsedCount;
  return true;
}

int32 UItemContainer::FindItem(int32 ItemID) const {
  if (const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID)) {
    return FoundSlots->Last(); // 返回最后一个槽位索引
  }
  return INDEX_NONE;
}

TArray<int32> UItemContainer::FindAllItems(int32 ItemID) const {
  if (const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID)) {
    return *FoundSlots; // 返回所有槽位索引
  }
  return {};
}
bool UItemContainer::IsSlotEmpty(int32 SlotIndex) const {
  return Slots.IsValidIndex(SlotIndex) && !SlotOccupied[SlotIndex];
}

bool UItemContainer::GetItemAtSlot(int32 SlotIndex,
                                   FItemInformation &OutItem) const {
  if (!Slots.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex])
    return false;
  OutItem = Slots[SlotIndex];
  return true;
}

bool UItemContainer::SwapSlots(int32 SlotA, int32 SlotB) {
  if (!Slots.IsValidIndex(SlotA) || !Slots.IsValidIndex(SlotB))
    return false;
  if (SlotA == SlotB)
    return true;
  if (SlotOccupied[SlotA]) {
    ItemIDToSlot.FindChecked(Slots[SlotA].ItemID).Remove(SlotA);
  }
  if (SlotOccupied[SlotB]) {
    ItemIDToSlot.FindChecked(Slots[SlotB].ItemID).Remove(SlotB);
  }
  Swap(Slots[SlotA], Slots[SlotB]);
  Swap(SlotOccupied[SlotA], SlotOccupied[SlotB]);

  if (SlotOccupied[SlotA]) {
    ItemIDToSlot.FindChecked(Slots[SlotA].ItemID).Add(SlotA);
  }
  if (SlotOccupied[SlotB]) {
    ItemIDToSlot.FindChecked(Slots[SlotB].ItemID).Add(SlotB);
  }
  return true;
}

void UItemContainer::ClearContainer() {
  const int32 Capacity = Slots.Num();
  for (int32 i = 0; i < Capacity; ++i) {
    Slots[i] = FItemInformation();
    SlotOccupied[i] = false;
  }
  FreeSlots.Reset(Capacity);
  FreeSlots.Reserve(Capacity);
  for (int32 i = Capacity - 1; i >= 0; --i) {
    FreeSlots.Add(i);
  }
  ItemIDToSlot.Empty(Capacity);
  UsedCount = 0;
}

bool UItemContainer::ResizeContainer(int32 NewCapacity) {
  if (NewCapacity <= 0)
    return false;

  const int32 OldCapacity = Slots.Num();
  if (NewCapacity == OldCapacity)
    return false;

  if (NewCapacity > OldCapacity) {
    // 扩容：新增的空槽加入 FreeSlots
    Slots.SetNum(NewCapacity);
    SlotOccupied.SetNum(NewCapacity);
    FreeSlots.Reserve(FreeSlots.Num() + (NewCapacity - OldCapacity));

    // 新槽位是空的，倒序加入让弹出顺序从 OldCapacity 开始
    for (int32 i = NewCapacity - 1; i >= OldCapacity; --i) {
      SlotOccupied[i] = false;
      FreeSlots.Add(i);
    }
  } else {
    if (NewCapacity < UsedCount) {
      UE_LOG(LogTemp, Warning,
             TEXT("ResizeContainer: 新容量 %d 小于已用数量 %d，拒绝缩容"),
             NewCapacity, UsedCount);
      return false;
    }

    // 把超出新容量的已占用物品搬到前面的空槽
    for (int32 i = NewCapacity; i < OldCapacity; ++i) {
      if (!SlotOccupied[i])
        continue;

      // 找一个 < NewCapacity 的空槽
      int32 Target = INDEX_NONE;
      for (int32 j = 0; j < NewCapacity; ++j) {
        if (!SlotOccupied[j]) {
          Target = j;
          break;
        }
      }
      if (Target == INDEX_NONE) {
        UE_LOG(LogTemp, Warning, TEXT("ResizeContainer: 无可用空槽，缩容失败"));
        return false;
      }

      Slots[Target] = Slots[i];
      SlotOccupied[Target] = true;
      ItemIDToSlot.FindChecked(Slots[i].ItemID).Remove(i);
      ItemIDToSlot.FindChecked(Slots[Target].ItemID).Add(Target);

      Slots[i] = FItemInformation();
      SlotOccupied[i] = false;
    }

    // 真正缩容
    Slots.SetNum(NewCapacity);
    SlotOccupied.SetNum(NewCapacity);

    // 重建 FreeSlots：只保留 < NewCapacity 的空槽
    FreeSlots.Reset();
    FreeSlots.Reserve(NewCapacity - UsedCount);
    for (int32 i = NewCapacity - 1; i >= 0; --i) {
      if (!SlotOccupied[i])
        FreeSlots.Add(i);
    }
  }
  return true;
}