// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Components/ItemContainer.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "ItemSystem/Database/ItemDatabaseSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"
#include "Utils/ComponentAuthorityGuard.h"

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

  // 内容的可见性由**类**决定（见 GetContentsReplicationCondition 的注释）：
  //   - 玩家背包 / 快捷栏：COND_OwnerOnly（隐私）；
  //   - 世界容器（UStorageContainer）：COND_None（范围内所有客户端，UI 才能显示）。
  // 权限（AccessMode）只门控交互，不负责隐藏内存里的数据。
  FDoRepLifetimeParams SlotsParams;
  SlotsParams.Condition = GetContentsReplicationCondition();
  DOREPLIFETIME_WITH_PARAMS(UItemContainer, Slots, SlotsParams);

  FDoRepLifetimeParams OccupiedParams;
  OccupiedParams.Condition = GetContentsReplicationCondition();
  DOREPLIFETIME_WITH_PARAMS(UItemContainer, SlotOccupied, OccupiedParams);

  // 权限数据很小，而且所有可能显示 / 交互的端都要读：不加 condition。
  DOREPLIFETIME(UItemContainer, AccessMode);
  DOREPLIFETIME(UItemContainer, AccessOwner);
  DOREPLIFETIME(UItemContainer, AllowedPlayers);
}

// ===== 访问权限 =====

APlayerController *UItemContainer::ResolveOwningPlayerController() const {
  AActor *OwnerActor = GetOwner();
  if (!OwnerActor) {
    return nullptr;
  }

  // 箱子的 owner 有时会被设置成 PlayerController（例如由某个玩家生成）：
  // 这时它本身就是那个玩家的身份，直接返回。
  if (APlayerController *OwningController =
          Cast<APlayerController>(OwnerActor)) {
    return OwningController;
  }

  // 玩家背包 / 快捷栏在角色的 Pawn 上：拥有者就是控制这个 Pawn 的 Controller。
  // 服务器端与拥有者客户端都能取到；模拟代理上 GetController() 为空（正确：
  // 别的玩家不能访问）。
  if (APawn *OwningPawn = Cast<APawn>(OwnerActor)) {
    return Cast<APlayerController>(OwningPawn->GetController());
  }

  return nullptr;
}

bool UItemContainer::IsOwnedBy(APlayerController *Player) const {
  if (!Player) {
    return false;
  }

  // 显式指定的 AccessOwner 优先：它用 PlayerState 作为复制身份，
  // 两端比较的是同一份对象（PlayerController 只在本地客户端存在）。
  if (AccessOwner != nullptr) {
    return AccessOwner == Player->GetPlayerState<APlayerState>();
  }

  // 没显式指定时，容器所在 Pawn 的 Controller 就是拥有者。
  return ResolveOwningPlayerController() == Player;
}

bool UItemContainer::CanBeAccessedBy_Implementation(
    APlayerController *Player) const {
  if (!Player) {
    return false;
  }

  switch (AccessMode) {
  case EContainerAccessMode::Public:
    return true;

  case EContainerAccessMode::Locked:
    return false;

  case EContainerAccessMode::OwnerOnly:
    return IsOwnedBy(Player);

  case EContainerAccessMode::Whitelist: {
    if (IsOwnedBy(Player)) {
      return true;
    }
    const APlayerState *PlayerState = Player->GetPlayerState<APlayerState>();
    if (!PlayerState) {
      return false;
    }
    return AllowedPlayers.ContainsByPredicate(
        [PlayerState](const TObjectPtr<APlayerState> &Entry) {
          return Entry.Get() == PlayerState;
        });
  }

  default:
    // 新增了模式但忘了实现：安全默认值 = 拒绝。
    return false;
  }
}

bool UItemContainer::HasAccess(APlayerController *Player) const {
  if (!Player) {
    return false;
  }
  return CanBeAccessedBy(Player);
}

void UItemContainer::SetAccessMode(EContainerAccessMode InAccessMode) {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());

  if (AccessMode == InAccessMode) {
    return;
  }

  const EContainerAccessMode OldMode = AccessMode;
  AccessMode = InAccessMode;

  WFLOG_INFO("[容器权限] %s 权限模式 %d -> %d（容器=%s）。",
             *GetNameSafe(GetOwner()), static_cast<int32>(OldMode),
             static_cast<int32>(AccessMode), *GetNameSafe(this));

  // 权限变化不改变内容，但 UI 可能显示「可访问 / 锁定」状态，借同一个广播刷新。
  NotifyContainerChanged();
}

void UItemContainer::SetAccessOwner(APlayerState *InOwner) {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());

  if (AccessOwner == InOwner) {
    return;
  }

  APlayerState *OldOwner = AccessOwner.Get();
  AccessOwner = InOwner;

  WFLOG_INFO("[容器权限] %s 的拥有者：%s -> %s（容器=%s）。",
             *GetNameSafe(GetOwner()), *GetNameSafe(OldOwner),
             *GetNameSafe(InOwner), *GetNameSafe(this));

  NotifyContainerChanged();
}

void UItemContainer::SetAccessOwnerController(APlayerController *InOwner) {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());

  APlayerState *NewOwner =
      InOwner ? InOwner->GetPlayerState<APlayerState>() : nullptr;
  if (InOwner && !NewOwner) {
    WFLOG_WARNING("[容器权限] %s 无法把玩家 %s 设为拥有者：该 Controller 还没有 "
                  "PlayerState（可能尚未初始化完成）。",
                  *GetNameSafe(GetOwner()), *GetNameSafe(InOwner));
  }

  SetAccessOwner(NewOwner);
}

void UItemContainer::AddAllowedPlayer(APlayerState *Player) {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());

  if (!Player) {
    WFLOG_WARNING("[容器权限] %s 收到空的 PlayerState，忽略加入白名单。",
                  *GetNameSafe(GetOwner()));
    return;
  }

  const bool bAlreadyAllowed = AllowedPlayers.ContainsByPredicate(
      [Player](const TObjectPtr<APlayerState> &Entry) {
        return Entry.Get() == Player;
      });
  if (bAlreadyAllowed) {
    WFLOG_INFO("[容器权限] %s 已经在 %s 的白名单里，忽略重复加入。",
               *GetNameSafe(Player), *GetNameSafe(GetOwner()));
    return;
  }

  AllowedPlayers.Add(Player);
  WFLOG_INFO("[容器权限] %s 加入 %s 的白名单（当前 %d 人）。",
             *GetNameSafe(Player), *GetNameSafe(GetOwner()),
             AllowedPlayers.Num());
  NotifyContainerChanged();
}

void UItemContainer::RemoveAllowedPlayer(APlayerState *Player) {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());

  if (!Player) {
    return;
  }

  const int32 RemovedCount = AllowedPlayers.RemoveAll(
      [Player](const TObjectPtr<APlayerState> &Entry) {
        return Entry.Get() == Player;
      });
  if (RemovedCount == 0) {
    WFLOG_INFO("[容器权限] %s 不在 %s 的白名单里，忽略移除请求。",
               *GetNameSafe(Player), *GetNameSafe(GetOwner()));
    return;
  }

  WFLOG_INFO("[容器权限] %s 移出 %s 的白名单（剩余 %d 人）。",
             *GetNameSafe(Player), *GetNameSafe(GetOwner()),
             AllowedPlayers.Num());
  NotifyContainerChanged();
}

void UItemContainer::ClearAllowedPlayers() {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());

  if (AllowedPlayers.Num() == 0) {
    return;
  }

  const int32 OldCount = AllowedPlayers.Num();
  AllowedPlayers.Reset();
  WFLOG_INFO("[容器权限] %s 清空白名单（原 %d 人）。",
             *GetNameSafe(GetOwner()), OldCount);
  NotifyContainerChanged();
}

bool UItemContainer::CanCurrentRpcCallerAccess() const {
  APlayerController *Caller = ResolveOwningPlayerController();

  // 世界箱子没有拥有者 Controller：它的 Server RPC 依赖 owning connection，
  // 引擎本来就会丢弃；真正的跨容器入口是玩家身上的交互组件。这里放行，
  // 由交互组件做权限 / 距离校验。
  if (!Caller) {
    return true;
  }

  if (HasAccess(Caller)) {
    return true;
  }

  WFLOG_WARNING("[容器权限] %s 拒绝了玩家 %s 的 Server RPC（权限模式=%d）。",
                *GetNameSafe(GetOwner()), *GetNameSafe(Caller),
                static_cast<int32>(AccessMode));
  return false;
}

void UItemContainer::OnRep_Slots() {
  // 容器状态只在游戏线程访问：开发期断言拦截跨线程误用（Shipping 下被裁掉）
  check(IsInGameThread());
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
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());
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

int32 UItemContainer::AddItemStack(const FItemInformation &Item,
                                   int32 Quantity) {
  WF_COMPONENT_AUTHORITY_GUARD(0);
  check(IsInGameThread());

  const int32 Added = AddItemStackInternal(Item, Quantity);
  if (Added > 0) {
    NotifyContainerChanged();
  }
  return Added;
}

int32 UItemContainer::AddItemStackInternal(const FItemInformation &Item,
                                           int32 Quantity) {
  if (Quantity <= 0) {
    return 0;
  }

  // 非可堆叠物品每格只放 1 个；可堆叠物品受 MaxStackSize 限制
  const int32 StackSize =
      Item.IsStackable ? FMath::Max(1, Item.MaxStackSize) : 1;

  int32 Remaining = Quantity;

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
  }

  return Quantity - Remaining;
}

int32 UItemContainer::AddItemByID(int32 ItemID, int32 Quantity) {
  WF_COMPONENT_AUTHORITY_GUARD(0);
  check(IsInGameThread());
  const UItemDatabaseSubsystem *Database = UItemDatabaseSubsystem::Get(this);
  if (!Database) {
    return 0;
  }
  const FItemInformation *Definition = Database->GetItemDefinition(ItemID);
  return Definition ? AddItemStack(*Definition, Quantity) : 0;
}

bool UItemContainer::RemoveItem(int32 SlotIndex, int32 Quantity) {
  WF_COMPONENT_AUTHORITY_GUARD(false);
  check(IsInGameThread());
  if (RemoveSlotQuantityInternal(SlotIndex, Quantity) <= 0) {
    return false;
  }
  NotifyContainerChanged();
  return true;
}

bool UItemContainer::RemoveAllItem(int32 ItemID, int32 Quantity) {
  WF_COMPONENT_AUTHORITY_GUARD(false);
  check(IsInGameThread());
  if (RemoveByItemIDInternal(ItemID, Quantity) <= 0) {
    return false;
  }
  NotifyContainerChanged();
  return true;
}

int32 UItemContainer::RemoveSlotQuantityInternal(int32 SlotIndex,
                                                 int32 Quantity) {
  if (!Slots.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex]) {
    return 0;
  }

  // 已占用槽位至少含 1 个，避免未初始化/异常的数量
  const int32 Current = FMath::Max(1, Slots[SlotIndex].ItemQuality);
  const int32 Take = Quantity < 0 ? Current : FMath::Min(Current, Quantity);
  if (Take <= 0) {
    return 0;
  }

  if (Take >= Current) {
    RemoveItemAtSlotInternal(SlotIndex); // 清空整槽
  } else {
    Slots[SlotIndex].ItemQuality = Current - Take; // 部分扣除，槽位保留
  }
  return Take;
}

int32 UItemContainer::RemoveByItemIDInternal(int32 ItemID, int32 Quantity) {
  const TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID);
  if (!FoundSlots || FoundSlots->Num() == 0) {
    return 0;
  }

  // 拷贝槽位列表：整槽移除会修改 ItemIDToSlot，直接遍历会失效
  const TArray<int32> SlotsToProcess = *FoundSlots;

  const bool bRemoveAll = Quantity < 0;
  int32 Remaining = bRemoveAll ? 0 : Quantity;
  int32 RemovedTotal = 0;

  // 从最后一个槽位往前扣，符合原 RemoveItem“删最后一个槽位”的直觉
  for (int32 i = SlotsToProcess.Num() - 1;
       i >= 0 && (bRemoveAll || Remaining > 0); --i) {
    const int32 Take = RemoveSlotQuantityInternal(SlotsToProcess[i],
                                                  bRemoveAll ? -1 : Remaining);
    if (Take <= 0) {
      continue;
    }
    RemovedTotal += Take;
    if (!bRemoveAll) {
      Remaining -= Take;
    }
  }

  return RemovedTotal;
}

void UItemContainer::DetachOccupiedSlotFromIndex(int32 SlotIndex) {
  if (!Slots.IsValidIndex(SlotIndex) ||
      !SlotOccupied.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex]) {
    WFLOG_WARNING("DetachOccupiedSlotFromIndex[%s]: 槽位 %d 不是有效的已占用槽位，"
                  "跳过索引摘除。",
                  *GetNameSafe(GetOwner()), SlotIndex);
    return;
  }

  const int32 ItemID = Slots[SlotIndex].ItemID;
  TArray<int32> *FoundSlots = ItemIDToSlot.Find(ItemID);
  if (!FoundSlots) {
    WFLOG_ERROR("DetachOccupiedSlotFromIndex[%s]: 槽位 %d 的 ItemID=%d 不在 "
                "ItemIDToSlot 索引里（派生状态损坏）。",
                *GetNameSafe(GetOwner()), SlotIndex, ItemID);
    return;
  }

  FoundSlots->Remove(SlotIndex);
  if (FoundSlots->Num() == 0) {
    ItemIDToSlot.Remove(ItemID);
  }
}

void UItemContainer::AttachOccupiedSlotToIndex(int32 SlotIndex) {
  if (!Slots.IsValidIndex(SlotIndex) ||
      !SlotOccupied.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex]) {
    WFLOG_WARNING("AttachOccupiedSlotToIndex[%s]: 槽位 %d 不是有效的已占用槽位，"
                  "跳过索引挂载。",
                  *GetNameSafe(GetOwner()), SlotIndex);
    return;
  }

  // AddUnique：正常路径不会重复，这里只是防止异常状态把同一个下标挂两次
  //（重复下标会让 FindItem 返回同一个槽位、RemoveByItemID 重复扣除）。
  ItemIDToSlot.FindOrAdd(Slots[SlotIndex].ItemID).AddUnique(SlotIndex);
}

bool UItemContainer::PlaceItemInEmptySlotInternal(
    int32 SlotIndex, const FItemInformation &Item) {
  if (!Slots.IsValidIndex(SlotIndex) ||
      !SlotOccupied.IsValidIndex(SlotIndex)) {
    WFLOG_WARNING("PlaceItemInEmptySlotInternal[%s]: 槽位 %d 越界（容量=%d），"
                  "拒绝写入。",
                  *GetNameSafe(GetOwner()), SlotIndex, Slots.Num());
    return false;
  }
  if (SlotOccupied[SlotIndex]) {
    WFLOG_WARNING("PlaceItemInEmptySlotInternal[%s]: 槽位 %d 已被占用，"
                  "拒绝覆盖。",
                  *GetNameSafe(GetOwner()), SlotIndex);
    return false;
  }

  if (Item.ItemID < 0) {
    // 仍然写入：这是「把已有物品挪走」的原语，丢掉它比保留一个无效项更糟。
    WFLOG_WARNING("PlaceItemInEmptySlotInternal[%s]: 槽位 %d 写入的 ItemID=%d "
                  "无效（物品数据可能已损坏），仍按原值保留。",
                  *GetNameSafe(GetOwner()), SlotIndex, Item.ItemID);
  }

  Slots[SlotIndex] = Item;
  SlotOccupied[SlotIndex] = true;
  AttachOccupiedSlotToIndex(SlotIndex);

  if (FreeSlots.RemoveSingle(SlotIndex) == 0) {
    WFLOG_WARNING("PlaceItemInEmptySlotInternal[%s]: 槽位 %d 标记为空闲但不在 "
                  "FreeSlots 里（派生状态不一致），已直接占用。",
                  *GetNameSafe(GetOwner()), SlotIndex);
  }
  ++UsedCount;
  return true;
}

bool UItemContainer::RemoveItemAtSlotInternal(int32 SlotIndex) {
  if (!Slots.IsValidIndex(SlotIndex))
    return false;
  if (!SlotOccupied.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex])
    return false;

  DetachOccupiedSlotFromIndex(SlotIndex);

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
  // 两个数组是分开复制的，长度理论上一致但各自查边界更稳：
  // 只检查 Slots 就按下标读 SlotOccupied，两数组一旦不等长就是越界读
  return Slots.IsValidIndex(SlotIndex) &&
         SlotOccupied.IsValidIndex(SlotIndex) && !SlotOccupied[SlotIndex];
}

bool UItemContainer::GetItemAtSlot(int32 SlotIndex,
                                   FItemInformation &OutItem) const {
  if (!Slots.IsValidIndex(SlotIndex))
    return false;
  if (!SlotOccupied.IsValidIndex(SlotIndex)) {
    // Slots 通过而 SlotOccupied 越界：旧写法在这里直接读越界内存
    //（读访问违规，bug-064 现场 stack 的第一受害者）。分两种情况：
    //   1) SlotOccupied 为空（Num==0）：已知的「初始化前窗口」——控件在角色 BeginPlay
    //      （InitializeContainer）之前就先构造并查询，容器此刻可能仍带着蓝图资产里
    //      保存的旧槽位元素（实测 Slots(30)/SlotOccupied(0)，见 bug-065）；按空槽
    //      处理，且只提示一次，避免每局刷 60+ 条噪声把真错误淹没。
    //   2) 其它不等长（部分缺失/错位）：真正的数据损坏，保持每次报错。
    if (SlotOccupied.Num() == 0) {
      static bool bWarnedUninitializedSlotArrays = false;
      if (!bWarnedUninitializedSlotArrays) {
        bWarnedUninitializedSlotArrays = true;
        WFLOG_WARNING("GetItemAtSlot[%s]: 容器尚未初始化（Slots 已有 %d 个元素、"
                      "SlotOccupied 为空），按空槽处理；该窗口预期消失在 BeginPlay 的 "
                      "InitializeContainer 之后（本条仅提示一次）",
                      *GetNameSafe(GetOwner()), Slots.Num());
      }
    } else {
      WFLOG_ERROR("GetItemAtSlot[%s]: Slots(%d) 与 SlotOccupied(%d) 长度不一致，"
                  "槽位 %d 不可查询",
                  *GetNameSafe(GetOwner()), Slots.Num(), SlotOccupied.Num(),
                  SlotIndex);
    }
    return false;
  }
  if (!SlotOccupied[SlotIndex])
    return false;
  OutItem = Slots[SlotIndex];
  return true;
}

bool UItemContainer::SwapSlots(int32 SlotA, int32 SlotB) {
  WF_COMPONENT_AUTHORITY_GUARD(false);
  check(IsInGameThread());
  if (!Slots.IsValidIndex(SlotA) || !Slots.IsValidIndex(SlotB))
    return false;
  if (!SlotOccupied.IsValidIndex(SlotA) ||
      !SlotOccupied.IsValidIndex(SlotB)) {
    WFLOG_ERROR("SwapSlots[%s]: SlotOccupied(%d) 与 Slots(%d) 长度不一致，"
                "无法交换槽位 %d / %d。",
                *GetNameSafe(GetOwner()), SlotOccupied.Num(), Slots.Num(), SlotA,
                SlotB);
    return false;
  }
  if (SlotA == SlotB)
    return true;

  // 先把两个槽位从 ItemIDToSlot 里摘掉，再交换数据，最后按交换后的物品重新挂回。
  // 两个槽位都占用时这样做才能正确处理「不同 ItemID 的交换」。
  if (SlotOccupied[SlotA]) {
    DetachOccupiedSlotFromIndex(SlotA);
  }
  if (SlotOccupied[SlotB]) {
    DetachOccupiedSlotFromIndex(SlotB);
  }

  Swap(Slots[SlotA], Slots[SlotB]);
  Swap(SlotOccupied[SlotA], SlotOccupied[SlotB]);

  if (SlotOccupied[SlotA]) {
    AttachOccupiedSlotToIndex(SlotA);
  }
  if (SlotOccupied[SlotB]) {
    AttachOccupiedSlotToIndex(SlotB);
  }
  NotifyContainerChanged();
  return true;
}

bool UItemContainer::MoveOrMergeItem(int32 FromSlot, int32 ToSlot) {
  WF_COMPONENT_AUTHORITY_GUARD(false);
  check(IsInGameThread());
  if (!Slots.IsValidIndex(FromSlot) || !Slots.IsValidIndex(ToSlot)) {
    return false;
  }
  if (!SlotOccupied.IsValidIndex(FromSlot) ||
      !SlotOccupied.IsValidIndex(ToSlot)) {
    WFLOG_ERROR("MoveOrMergeItem[%s]: SlotOccupied(%d) 与 Slots(%d) 长度不一致，"
                "无法处理 %d -> %d。",
                *GetNameSafe(GetOwner()), SlotOccupied.Num(), Slots.Num(),
                FromSlot, ToSlot);
    return false;
  }
  if (FromSlot == ToSlot) {
    return false;
  }
  if (!SlotOccupied[FromSlot]) {
    return false; // 源槽位已空（拖拽期间被别处改动），无事可做
  }

  // 目标空槽：交换即可（等价于把源物品挪过去，并顺带带走空槽）
  if (!SlotOccupied[ToSlot]) {
    return SwapSlots(FromSlot, ToSlot);
  }

  FItemInformation Source;
  FItemInformation Target;
  GetItemAtSlot(FromSlot, Source);
  GetItemAtSlot(ToSlot, Target);

  // 数量沿用 ItemQuality；已占用槽位至少 1 个，避免异常数量
  const int32 SourceCount = FMath::Max(1, Source.ItemQuality);
  const int32 TargetCount = FMath::Max(1, Target.ItemQuality);

  // 只有「同 ItemID + 可堆叠 + 目标未满堆」才合并；不可堆叠、不同物品、
  // 或目标已是满堆（数量达到最大可堆叠数量）一律交换。
  const bool bSameItem = Source.ItemID == Target.ItemID;
  const bool bStackable = Source.IsStackable && Target.IsStackable;
  const int32 StackSize = bStackable ? FMath::Max(1, Target.MaxStackSize) : 1;

  if (!bSameItem || !bStackable || TargetCount >= StackSize) {
    return SwapSlots(FromSlot, ToSlot);
  }

  // 合并：只在目标与源两个槽位之间重分配数量——目标加到装满为止，剩下的留在源。
  // 两个槽位本来就都占用且 ItemID 相同，所以本次操作：
  //   - ItemIDToSlot 不变（ItemID 一个没变）
  //   - UsedCount 不变；只有源被清空时才回收槽位
  // 因此不需要 RebuildDerivedState，也不碰 FreeSlots，只需最后广播一次。
  //
  // 为什么不用 RemoveItemAtSlotInternal(源) + AddItemStack(源物品, 源数量)：
  // AddItemStack 是「按定义把数量重新堆到任意同 ItemID 的槽位」——它会遍历
  // ItemIDToSlot，可能先填满第三个同 ID 的未满堆，而不是本次拖拽的目标槽；
  // 而且中间会让 UsedCount 先减后加、把刚释放的源槽重新分配一次。拖拽合并的
  // 语义是「只有这两个槽位之间移动数量」，直接改数量既精确又原子。
  const int32 Move = FMath::Min(SourceCount, StackSize - TargetCount);

  Slots[ToSlot].ItemQuality = TargetCount + Move;   // 目标：装满
  Slots[FromSlot].ItemQuality = SourceCount - Move; // 源：留下多余的数量

  // 源被清空（数量全部并入），释放该槽位，避免留下一个占用但数量为 0 的槽
  if (Slots[FromSlot].ItemQuality <= 0) {
    RemoveItemAtSlotInternal(FromSlot);
  }

  NotifyContainerChanged();
  return true;
}

bool UItemContainer::MoveItemToContainer(int32 FromSlot,
                                         UItemContainer *TargetContainer,
                                         int32 ToSlot) {
  WF_COMPONENT_AUTHORITY_GUARD(false);
  check(IsInGameThread());

  if (!TargetContainer) {
    WFLOG_WARNING("[容器] %s 收到空的转移到目标容器，忽略。",
                  *GetNameSafe(GetOwner()));
    return false;
  }
  if (!IsAuthoritativeForActorComponent(TargetContainer)) {
    WFLOG_ERROR("[容器] %s 试图把物品转移到非权威端的容器 %s，已拒绝。",
                *GetNameSafe(GetOwner()),
                *GetNameSafe(TargetContainer->GetOwner()));
    return false;
  }
  if (!Slots.IsValidIndex(FromSlot) ||
      !SlotOccupied.IsValidIndex(FromSlot)) {
    WFLOG_WARNING("[容器] %s 的源槽位 %d 越界（容量=%d），忽略转移。",
                  *GetNameSafe(GetOwner()), FromSlot, Slots.Num());
    return false;
  }
  if (!SlotOccupied[FromSlot]) {
    return false; // 源槽位已空（拖拽期间被别处改动），无事可做
  }
  if (!TargetContainer->Slots.IsValidIndex(ToSlot) ||
      !TargetContainer->SlotOccupied.IsValidIndex(ToSlot)) {
    WFLOG_WARNING("[容器] %s 的目标槽位 %d 越界（容量=%d），忽略转移。",
                  *GetNameSafe(TargetContainer->GetOwner()), ToSlot,
                  TargetContainer->Slots.Num());
    return false;
  }

  // 同一个容器：直接复用「放置」规则，避免维护两套合并 / 交换逻辑。
  if (TargetContainer == this) {
    return MoveOrMergeItem(FromSlot, ToSlot);
  }

  const FItemInformation SourceItem = Slots[FromSlot];
  const int32 SourceCount = FMath::Max(1, SourceItem.ItemQuality);

  // 1) 目标为空槽：移动到目标，再释放源槽位。
  if (!TargetContainer->SlotOccupied[ToSlot]) {
    if (!TargetContainer->PlaceItemInEmptySlotInternal(ToSlot, SourceItem)) {
      return false; // 内部已经记了日志
    }

    if (!RemoveItemAtSlotInternal(FromSlot)) {
      // 正常情况下不会发生（源槽在入口已确认占用）。真的发生时必须回滚目标，
      // 否则同一件物品会同时存在于两个容器里（复制 bug，比失败更糟）。
      TargetContainer->RemoveItemAtSlotInternal(ToSlot);
      WFLOG_ERROR("[容器] %s[%d] -> %s[%d] 移动时源槽移除失败，已回滚目标槽。",
                  *GetNameSafe(this), FromSlot,
                  *GetNameSafe(TargetContainer), ToSlot);
      return false;
    }

    NotifyContainerChanged();
    TargetContainer->NotifyContainerChanged();
    WFLOG_INFO("[容器] 移动物品：%s[%d] -> %s[%d]（ItemID=%d，数量=%d）。",
               *GetNameSafe(this), FromSlot, *GetNameSafe(TargetContainer),
               ToSlot, SourceItem.ItemID, SourceCount);
    return true;
  }

  const FItemInformation TargetItem = TargetContainer->Slots[ToSlot];
  const int32 TargetCount = FMath::Max(1, TargetItem.ItemQuality);

  // 2) 同 ItemID + 可堆叠 + 目标未满：把源数量并入目标，余数留在源槽位。
  //    与 MoveOrMergeItem 完全同一条规则（只在两个槽位之间重分配数量）。
  const bool bSameItem = SourceItem.ItemID == TargetItem.ItemID;
  const bool bStackable = SourceItem.IsStackable && TargetItem.IsStackable;
  const int32 StackSize =
      bStackable ? FMath::Max(1, TargetItem.MaxStackSize) : 1;

  if (bSameItem && bStackable && TargetCount < StackSize) {
    const int32 Move = FMath::Min(SourceCount, StackSize - TargetCount);
    TargetContainer->Slots[ToSlot].ItemQuality = TargetCount + Move;
    Slots[FromSlot].ItemQuality = SourceCount - Move;

    if (Slots[FromSlot].ItemQuality <= 0) {
      RemoveItemAtSlotInternal(FromSlot);
    }

    NotifyContainerChanged();
    TargetContainer->NotifyContainerChanged();
    WFLOG_INFO("[容器] 合并物品：%s[%d] -> %s[%d]（ItemID=%d，移动数量=%d，"
               "源剩余=%d，目标=%d）。",
               *GetNameSafe(this), FromSlot, *GetNameSafe(TargetContainer),
               ToSlot, SourceItem.ItemID, Move,
               FMath::Max(0, Slots[FromSlot].ItemQuality),
               TargetContainer->Slots[ToSlot].ItemQuality);
    return true;
  }

  // 3) 其它情况：两个槽位交换（不同物品 / 不可堆叠 / 目标已满堆）。
  //    两边都确认占用，所以 SlotOccupied 不需要交换；只交换物品数据，
  //    并让两个容器的 ItemIDToSlot 索引按交换后的 ItemID 重新挂载。
  DetachOccupiedSlotFromIndex(FromSlot);
  TargetContainer->DetachOccupiedSlotFromIndex(ToSlot);

  Swap(Slots[FromSlot], TargetContainer->Slots[ToSlot]);

  AttachOccupiedSlotToIndex(FromSlot);
  TargetContainer->AttachOccupiedSlotToIndex(ToSlot);

  NotifyContainerChanged();
  TargetContainer->NotifyContainerChanged();
  WFLOG_INFO("[容器] 交换物品：%s[%d] <-> %s[%d]（ItemID %d <-> %d）。",
             *GetNameSafe(this), FromSlot, *GetNameSafe(TargetContainer),
             ToSlot, SourceItem.ItemID, TargetItem.ItemID);
  return true;
}

int32 UItemContainer::TransferAllToContainer(
    UItemContainer *TargetContainer) {
  WF_COMPONENT_AUTHORITY_GUARD(0);
  check(IsInGameThread());

  if (!TargetContainer) {
    WFLOG_WARNING("[容器] %s 收到空的全体转移目标容器，忽略。",
                  *GetNameSafe(GetOwner()));
    return 0;
  }
  if (TargetContainer == this) {
    WFLOG_WARNING("[容器] %s 的全体转移源与目标是同一个容器，忽略。",
                  *GetNameSafe(GetOwner()));
    return 0;
  }
  if (!IsAuthoritativeForActorComponent(TargetContainer)) {
    WFLOG_ERROR("[容器] %s 试图把物品全体转移到非权威端的容器 %s，已拒绝。",
                *GetNameSafe(GetOwner()),
                *GetNameSafe(TargetContainer->GetOwner()));
    return 0;
  }

  // 快照源槽位：转移会清空 / 改写源槽位，不能边遍历边改。
  struct FSourceStack {
    int32 SlotIndex = INDEX_NONE;
    FItemInformation Item;
  };
  TArray<FSourceStack> SourceStacks;
  SourceStacks.Reserve(UsedCount);

  int32 TotalInSource = 0;
  for (int32 SlotIndex = 0; SlotIndex < Slots.Num(); ++SlotIndex) {
    if (!SlotOccupied.IsValidIndex(SlotIndex) || !SlotOccupied[SlotIndex]) {
      continue;
    }
    FSourceStack &Stack = SourceStacks.AddDefaulted_GetRef();
    Stack.SlotIndex = SlotIndex;
    Stack.Item = Slots[SlotIndex];
    TotalInSource += FMath::Max(1, Slots[SlotIndex].ItemQuality);
  }

  if (SourceStacks.Num() == 0) {
    WFLOG_INFO("[容器] %s 是空的，全体转移无事可做。",
               *GetNameSafe(GetOwner()));
    return 0;
  }

  int32 TotalMoved = 0;
  bool bSourceChanged = false;
  bool bTargetChanged = false;

  for (const FSourceStack &Stack : SourceStacks) {
    const int32 SourceCount = FMath::Max(1, Stack.Item.ItemQuality);

    // 复用 AddItemStack 的规则：先补同 ItemID 的未满堆，溢出再放空槽。
    // 这一步只维护目标容器的派生状态，不广播（循环结束后统一广播一次）。
    const int32 Moved =
        TargetContainer->AddItemStackInternal(Stack.Item, SourceCount);
    if (Moved <= 0) {
      // 这一堆完全放不下（背包已满且没有可合并的未满堆），继续尝试后面的
      // 物品——不同物品可能有不同的堆叠容量 / 可堆叠性。
      continue;
    }

    TotalMoved += Moved;
    bSourceChanged = true;
    bTargetChanged = true;

    if (!Slots.IsValidIndex(Stack.SlotIndex) ||
        !SlotOccupied.IsValidIndex(Stack.SlotIndex) ||
        !SlotOccupied[Stack.SlotIndex]) {
      // 快照来自本容器且整个过程在游戏线程同步执行，正常不会走到这里。
      // 真出现时不能再动源槽位，否则会重复 / 丢失物品，必须留下 ERROR 便于排查。
      WFLOG_ERROR("[容器] %s 全体转移时源槽位 %d 已失效，跳过移除"
                  "（可能出现重复物品，请检查容器状态）。",
                  *GetNameSafe(GetOwner()), Stack.SlotIndex);
      continue;
    }

    if (Moved >= SourceCount) {
      RemoveItemAtSlotInternal(Stack.SlotIndex);
    } else {
      // 只转移了一部分（目标在装满前还有局部空间）：剩余数量留在源槽位。
      Slots[Stack.SlotIndex].ItemQuality = SourceCount - Moved;
    }
  }

  // 两个容器各广播一次：槽位控件自己按 (容器, 索引) 拉数据并 diff，
  // 不需要为每一堆都广播一轮。
  if (bSourceChanged) {
    NotifyContainerChanged();
  }
  if (bTargetChanged) {
    TargetContainer->NotifyContainerChanged();
  }

  const int32 Remaining = TotalInSource - TotalMoved;
  if (Remaining > 0) {
    WFLOG_WARNING("[容器] %s 全体转移：目标 %s 只放得下 %d/%d 个物品，"
                  "剩余 %d 个留在源容器（背包已满或没有可合并的未满堆）。",
                  *GetNameSafe(GetOwner()),
                  *GetNameSafe(TargetContainer->GetOwner()), TotalMoved,
                  TotalInSource, Remaining);
  } else {
    WFLOG_INFO("[容器] %s 全体转移完成：%d 个物品 -> %s。",
               *GetNameSafe(GetOwner()), TotalMoved,
               *GetNameSafe(TargetContainer->GetOwner()));
  }

  return TotalMoved;
}

void UItemContainer::ClearContainer() {
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());
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
  WF_COMPONENT_AUTHORITY_GUARD(false);
  check(IsInGameThread());
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
      WFLOG_WARNING("ResizeContainer: 新容量 %d 小于已用数量 %d，拒绝缩容",
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
        WFLOG_WARNING("ResizeContainer: 无可用空槽，缩容失败");
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
  WF_COMPONENT_AUTHORITY_GUARD(void());
  check(IsInGameThread());
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
      WFLOG_WARNING("OrganizeContainer: 槽位不足，%d 个物品被丢弃",
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
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  AddItemByID(ItemID, Quantity);
}

bool UItemContainer::Server_RemoveItem_Validate(int32 SlotIndex,
                                                int32 Quantity) {
  // Quantity 为负表示清空整槽（-1 为约定值）；只对正数设上限防止滥用
  return SlotIndex >= 0 && Quantity <= 10000;
}

void UItemContainer::Server_RemoveItem_Implementation(int32 SlotIndex,
                                                      int32 Quantity) {
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  RemoveItem(SlotIndex, Quantity);
}

bool UItemContainer::Server_RemoveAllItem_Validate(int32 ItemID,
                                                   int32 Quantity) {
  return ItemID >= 0 && Quantity <= 10000;
}

void UItemContainer::Server_RemoveAllItem_Implementation(int32 ItemID,
                                                         int32 Quantity) {
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  RemoveAllItem(ItemID, Quantity);
}

bool UItemContainer::Server_SwapSlots_Validate(int32 SlotA, int32 SlotB) {
  return SlotA >= 0 && SlotB >= 0;
}

void UItemContainer::Server_SwapSlots_Implementation(int32 SlotA, int32 SlotB) {
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  SwapSlots(SlotA, SlotB);
}

bool UItemContainer::Server_MoveOrMerge_Validate(int32 FromSlot, int32 ToSlot) {
  // 只做廉价检查；槽位有效性/占用/堆叠规则全在 MoveOrMergeItem 内部判定
  return FromSlot >= 0 && ToSlot >= 0;
}

void UItemContainer::Server_MoveOrMerge_Implementation(int32 FromSlot,
                                                       int32 ToSlot) {
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  MoveOrMergeItem(FromSlot, ToSlot);
}

bool UItemContainer::Server_OrganizeContainer_Validate() { return true; }

void UItemContainer::Server_OrganizeContainer_Implementation() {
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  OrganizeContainer();
}

bool UItemContainer::Server_ClearContainer_Validate() { return true; }

void UItemContainer::Server_ClearContainer_Implementation() {
  if (!CanCurrentRpcCallerAccess()) {
    return;
  }
  ClearContainer();
}