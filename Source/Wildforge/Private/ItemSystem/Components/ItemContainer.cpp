// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Components/ItemContainer.h"

#include "ItemSystem/Database/ItemDatabaseSubsystem.h"
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

void UItemContainer::OnRep_Slots() {
  RebuildDerivedState();
  NotifyContainerChanged();
}

void UItemContainer::NotifyContainerChanged() {
  OnContainerChanged.Broadcast();
}

void UItemContainer::RebuildDerivedState() {
  // 派生数据全部可由 Slots + SlotOccupied 重算，客户端只做这件事
  const int32 Capacity = Slots.Num();

  FreeSlots.Reset(Capacity);
  FreeSlots.Reserve(Capacity);
  ItemIDToSlot.Empty(Capacity);
  ItemIDToSlot.Reserve(Capacity);
  UsedCount = 0;

  // 倒序遍历，使 FreeSlots 弹出顺序与服务器 InitializeContainer 一致（先弹下标
  // 0）
  for (int32 i = Capacity - 1; i >= 0; --i) {
    // SlotOccupied 与 Slots
    // 同批复制，正常等长；不齐时按空槽处理，等补齐后重算幂等
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

  NotifyContainerChanged();
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

  NotifyContainerChanged();
  return true;
}

int32 UItemContainer::AddItemStack(const FItemInformation &Item,
                                   int32 Quantity) {
  if (Quantity <= 0) {
    return 0;
  }

  // 非可堆叠物品每格只放 1 个；可堆叠物品受 MaxStackSize 限制
  const int32 StackSize =
      Item.IsStackable ? FMath::Max(1, Item.MaxStackSize) : 1;

  int32 Remaining = Quantity;
  bool bChanged = false;

  // 1) 优先补进已有的同 ItemID 未满堆
  if (Item.IsStackable) {
    if (const TArray<int32> *Existing = ItemIDToSlot.Find(Item.ItemID)) {
      for (int32 SlotIndex : *Existing) {
        if (Remaining <= 0) {
          break;
        }
        if (!Slots.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex]) {
          continue;
        }
        // 已占用槽位至少含 1 个，避免未初始化/异常的数量
        const int32 Current = FMath::Max(1, Slots[SlotIndex].ItemQuality);
        if (Current >= StackSize) {
          continue;
        }
        const int32 Add = FMath::Min(StackSize - Current, Remaining);
        Slots[SlotIndex].ItemQuality = Current + Add;
        Remaining -= Add;
        bChanged = true;
      }
    }
  }

  // 2) 溢出部分放入空槽，每次放入一整堆
  while (Remaining > 0) {
    if (FreeSlots.Num() == 0) {
      break; // 背包已满，丢弃剩余
    }

    const int32 TargetSlot = FreeSlots.Pop(EAllowShrinking::No);
    const int32 ThisCount = FMath::Min(StackSize, Remaining);

    FItemInformation NewItem = Item;
    NewItem.ItemQuality = ThisCount; // 数量沿用 ItemQuality

    Slots[TargetSlot] = NewItem;
    SlotOccupied[TargetSlot] = true;
    ItemIDToSlot.FindOrAdd(Item.ItemID).Add(TargetSlot);
    ++UsedCount;

    Remaining -= ThisCount;
    bChanged = true;
  }

  if (bChanged) {
    NotifyContainerChanged();
  }

  return Quantity - Remaining;
}

bool UItemContainer::RemoveItem(int32 ItemID) {
  if (const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID)) {
    return RemoveItemAtSlot(FoundSlots->Last()); // 删除最后一个槽位
  }
  return false;
}

bool UItemContainer::RemoveAllItem(int32 ItemID) {
  const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID);
  if (!FoundSlots || FoundSlots->Num() == 0)
    return false;

  // 先拷贝槽位列表：RemoveItemAtSlotInternal 会修改 ItemIDToSlot，
  // 直接遍历 *FoundSlots 会因 map 变动而失效
  const TArray<int32> SlotsToRemove = *FoundSlots;

  bool bRemovedAny = false;
  for (int32 SlotIndex : SlotsToRemove) {
    bRemovedAny |= RemoveItemAtSlotInternal(SlotIndex);
  }

  // 批量删除只广播一次
  if (bRemovedAny) {
    NotifyContainerChanged();
  }
  return bRemovedAny;
}

bool UItemContainer::RemoveItemAtSlot(int32 SlotIndex) {
  if (!RemoveItemAtSlotInternal(SlotIndex)) {
    return false;
  }
  NotifyContainerChanged();
  return true;
}

bool UItemContainer::RemoveItemAtSlotInternal(int32 SlotIndex) {
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
  NotifyContainerChanged();
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

  NotifyContainerChanged();
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
  NotifyContainerChanged();
  return true;
}

void UItemContainer::OrganizeContainer() {
  const int32 Capacity = Slots.Num();
  if (Capacity <= 0)
    return;

  // 每个 ItemID 汇总后的结果：一个模板（取该 ItemID 首次出现的物品数据）
  // 和总数量（数量暂用 ItemQuality 表示）
  struct FItemGroup {
    FItemInformation Template;
    int32 TotalCount = 0;
    bool bHasTemplate = false;
  };
  TMap<int32, FItemGroup> Groups;

  for (int32 i = 0; i < Capacity; ++i) {
    if (!SlotOccupied[i])
      continue;
    // 丢弃无效项（ItemID == -1 / INDEX_NONE）
    if (Slots[i].ItemID == INDEX_NONE)
      continue;

    FItemGroup &Group = Groups.FindOrAdd(Slots[i].ItemID);
    if (!Group.bHasTemplate) {
      Group.Template = Slots[i];
      Group.bHasTemplate = true;
    }
    // 已占用的槽位至少含 1 个，避免未初始化/异常的数量丢失物品
    Group.TotalCount += FMath::Max(1, Slots[i].ItemQuality);
  }

  // 输出堆叠：同一 ItemID 按 可堆叠/最大堆叠数量 切分，满堆在前、余数堆在后
  struct FOutStack {
    FItemInformation Item;
    int32 Count = 0;
    int32 ItemID = INDEX_NONE;
  };
  TArray<FOutStack> Stacks;
  Stacks.Reserve(Capacity);

  for (const TPair<int32, FItemGroup> &Pair : Groups) {
    const FItemInformation &Template = Pair.Value.Template;
    const int32 ChunkSize =
        Template.IsStackable ? FMath::Max(1, Template.MaxStackSize) : 1;

    int32 Remaining = Pair.Value.TotalCount;
    while (Remaining > 0) {
      const int32 ThisCount = FMath::Min(ChunkSize, Remaining);

      FOutStack Stack;
      Stack.Item = Template;
      Stack.Item.ItemQuality = ThisCount; // 数量沿用 ItemQuality
      Stack.Count = ThisCount;
      Stack.ItemID = Pair.Key;
      Stacks.Add(MoveTemp(Stack));

      Remaining -= ThisCount;
    }
  }

  // 排序：ItemName 升序 -> ItemID 升序(同名的不同物品各自成组) ->
  // 数量降序(满堆在前)
  Stacks.Sort([](const FOutStack &A, const FOutStack &B) {
    const int32 NameCompare = A.Item.ItemName.ToString().Compare(
        B.Item.ItemName.ToString(), ESearchCase::IgnoreCase);
    if (NameCompare != 0)
      return NameCompare < 0;
    if (A.ItemID != B.ItemID)
      return A.ItemID < B.ItemID;
    return A.Count > B.Count;
  });

  // 紧凑写回：物品靠前，空槽集中到末尾
  int32 WriteIndex = 0;
  for (const FOutStack &Stack : Stacks) {
    if (WriteIndex >= Capacity) {
      UE_LOG(LogTemp, Warning,
             TEXT("OrganizeContainer: 槽位不足，%d 个物品被丢弃"),
             Stacks.Num() - WriteIndex);
      break;
    }
    Slots[WriteIndex] = Stack.Item;
    SlotOccupied[WriteIndex] = true;
    ++WriteIndex;
  }
  for (int32 i = WriteIndex; i < Capacity; ++i) {
    Slots[i] = FItemInformation();
    SlotOccupied[i] = false;
  }

  RebuildDerivedState();
  NotifyContainerChanged();
}

// ===== 客户端 -> 服务器：背包操作请求 (RPC) =====
// 声明在容器组件上，所有拥有容器的类自动继承。_Implementation 里直接调用本类
// 的权威方法（服务器/单机宿主上 Server RPC 会就地执行）。

bool UItemContainer::Server_AddItem_Validate(int32 ItemID, int32 Quantity) {
  // 只做廉价检查：ID 非负、数量为正，并设上限防止滥用
  return ItemID >= 0 && Quantity > 0 && Quantity <= 10000;
}

void UItemContainer::Server_AddItem_Implementation(int32 ItemID,
                                                   int32 Quantity) {
  const UItemDatabaseSubsystem *Database = UItemDatabaseSubsystem::Get(this);
  if (!Database) {
    return;
  }
  if (const FItemInformation *Definition =
          Database->GetItemDefinition(ItemID)) {
    AddItemStack(*Definition, Quantity);
  }
}

bool UItemContainer::Server_RemoveItemAtSlot_Validate(int32 SlotIndex) {
  return SlotIndex >= 0;
}

void UItemContainer::Server_RemoveItemAtSlot_Implementation(int32 SlotIndex) {
  RemoveItemAtSlot(SlotIndex);
}

bool UItemContainer::Server_RemoveItem_Validate(int32 ItemID) {
  return ItemID >= 0;
}

void UItemContainer::Server_RemoveItem_Implementation(int32 ItemID) {
  RemoveItem(ItemID);
}

bool UItemContainer::Server_RemoveAllItem_Validate(int32 ItemID) {
  return ItemID >= 0;
}

void UItemContainer::Server_RemoveAllItem_Implementation(int32 ItemID) {
  RemoveAllItem(ItemID);
}

bool UItemContainer::Server_SwapSlots_Validate(int32 SlotA, int32 SlotB) {
  return SlotA >= 0 && SlotB >= 0;
}

void UItemContainer::Server_SwapSlots_Implementation(int32 SlotA, int32 SlotB) {
  SwapSlots(SlotA, SlotB);
}

bool UItemContainer::Server_OrganizeContainer_Validate() { return true; }

void UItemContainer::Server_OrganizeContainer_Implementation() {
  OrganizeContainer();
}

bool UItemContainer::Server_ClearContainer_Validate() { return true; }

void UItemContainer::Server_ClearContainer_Implementation() {
  ClearContainer();
}