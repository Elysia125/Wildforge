// Fill out your copyright notice in the Description page of Project Settings.

#include "World/ChestActor.h"

#include "GameFramework/PlayerController.h"
#include "ItemSystem/Components/StorageContainer.h"
#include "ItemSystem/Database/ItemDatabaseSubsystem.h"
#include "ItemSystem/Enums/ItemRarity.h"
#include "ItemSystem/Structs/ItemInfo.h"
#include "Net/UnrealNetwork.h"
#include "Utils/WildforgeLog.h"
#include "World/Settings/ChestSettings.h"
#include "World/UI/ChestWindowWidget.h"

namespace {

/**
 * 等级 -> 可掉落稀有度池。
 *
 * 只做「适合这一档」的映射，不做权重 / 概率：同一档内所有物品等概率。
 * 掉落数量由 `LootCountMin / LootCountMax` 控制，掉落表本身来自 DT_Items。
 *
 *   Common    -> Common / Uncommon
 *   Rare      -> Rare / Epic
 *   Legendary -> Epic / Legendary
 */
bool IsRaritySuitableForChestLevel(EChestLevel Level, EItemRarity Rarity) {
  switch (Level) {
  case EChestLevel::Common:
    return Rarity == EItemRarity::Common || Rarity == EItemRarity::Uncommon;

  case EChestLevel::Rare:
    return Rarity == EItemRarity::Rare || Rarity == EItemRarity::Epic;

  case EChestLevel::Legendary:
    return Rarity == EItemRarity::Epic || Rarity == EItemRarity::Legendary;

  default:
    return false;
  }
}

} // namespace

AChestActor::AChestActor() {
  // 宝箱没有逐帧逻辑：所有工作都在 BeginPlay（生成掉落）与交互事件里完成。
  PrimaryActorTick.bCanEverTick = false;

  // 容器组件与 Actor 的复制前提：Actor 不复制，组件的所有 DOREPLIFETIME 都
  // 不会生效（见 bug-093）。
  bReplicates = true;

  Container = CreateDefaultSubobject<UStorageContainer>(TEXT("Container"));
}

void AChestActor::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 等级与窗口类都要让客户端知道：等级用于配色 / 表现，窗口类用于创建 UI。
  DOREPLIFETIME(AChestActor, ChestLevel);
  DOREPLIFETIME(AChestActor, ChestWindowClass);

  // 打开状态驱动各端的开 / 关箱表现；后续加入的客户端也能通过初始复制
  // 收到当前状态（值变化时触发 OnRep）。
  DOREPLIFETIME(AChestActor, bIsOpened);
}

void AChestActor::BeginPlay() {
  Super::BeginPlay();

  if (!HasAuthority()) {
    WFLOG_INFO("[宝箱] %s 客户端 BeginPlay：不初始化容器、不生成战利品，"
               "等待服务器复制（等级=%d）。",
               *GetName(), static_cast<int32>(ChestLevel));
    return;
  }

  WFLOG_INFO("[宝箱] %s 服务器 BeginPlay：等级=%d，容量类默认值=%d，"
             "战利品数量类默认值=%d~%d，自动生成=%s。",
             *GetName(), static_cast<int32>(ChestLevel), Capacity, LootCountMin,
             LootCountMax, bGenerateLootOnBeginPlay ? TEXT("是") : TEXT("否"));

  if (!Container) {
    WFLOG_ERROR("[宝箱] %s 没有 Container（UStorageContainer）组件，"
                "宝箱不可用。",
                *GetName());
    return;
  }

  ApplyGameplaySettingsOverrides();

  // Actor 级的权限模式是宝箱的「业务配置」，容器组件上的同名属性是通用默认值；
  // 对宝箱来说以 Actor 为准，避免策划在容器组件上误留 OwnerOnly 导致谁都打不开。
  Container->SetAccessMode(AccessMode);

  if (Container->GetCapacity() > 0) {
    // 蓝图 / 其它系统已经初始化过：不要重复 InitializeContainer，
    // 那会把已有物品全部清空。
    WFLOG_WARNING("[宝箱] %s 的容器在 BeginPlay 前已被初始化（容量 %d），"
                  "跳过 InitializeContainer，避免清空已有内容。",
                  *GetName(), Container->GetCapacity());
  } else if (Capacity > 0) {
    Container->InitializeContainer(Capacity);
    WFLOG_INFO("[宝箱] %s 初始化容器：容量 %d 格。", *GetName(), Capacity);
  } else {
    WFLOG_ERROR("[宝箱] %s 的容量 = %d（必须 > 0），容器不会初始化，"
                "宝箱无法使用。",
                *GetName(), Capacity);
    return;
  }

  if (bGenerateLootOnBeginPlay) {
    GenerateLoot();
  } else {
    WFLOG_INFO("[宝箱] %s 关闭了自动生成战利品"
               "（bGenerateLootOnBeginPlay=false）。",
               *GetName());
  }
}

void AChestActor::ApplyGameplaySettingsOverrides() {
  // BeginPlay 的权威分支已经判断过；这里再守一次，避免以后被其它服务器流程
  // 误调时悄悄读到客户端本地 ini。
  if (!HasAuthority()) {
    return;
  }

  const UChestSettings *Settings = UChestSettings::Get();
  if (!Settings) {
    WFLOG_ERROR("[配置] %s 取不到 UChestSettings（CDO 为空），保留类默认值"
                "（容量 %d，战利品 %d~%d）。",
                *GetName(), Capacity, LootCountMin, LootCountMax);
    return;
  }

  // 每一档都独立判断覆盖开关：ini 的键名就是属性名，改错开关会静默失效
  // （见 bug-052）。
  if (Settings->bOverride_Capacity) {
    const int32 OldCapacity = Capacity;
    // 与配置头文件的 ClampMin 保持一致：ini 是手写文本，绕过编辑器面板的 Clamp。
    Capacity = FMath::Max(1, Settings->Capacity);
    WFLOG_INFO("[配置] %s 宝箱容量被 ini 覆盖：类默认值 %d -> 生效 %d"
               "（ini=%d）。",
               *GetName(), OldCapacity, Capacity, Settings->Capacity);
  } else {
    WFLOG_INFO("[配置] %s 宝箱容量没有 ini 覆盖，用类默认值 %d。", *GetName(),
               Capacity);
  }

  if (Settings->bOverride_LootCountMin) {
    const int32 OldMin = LootCountMin;
    LootCountMin = FMath::Max(0, Settings->LootCountMin);
    WFLOG_INFO("[配置] %s 战利品数量下限被 ini 覆盖：类默认值 %d -> 生效 %d"
               "（ini=%d）。",
               *GetName(), OldMin, LootCountMin, Settings->LootCountMin);
  } else {
    WFLOG_INFO("[配置] %s 战利品数量下限没有 ini 覆盖，用类默认值 %d。",
               *GetName(), LootCountMin);
  }

  if (Settings->bOverride_LootCountMax) {
    const int32 OldMax = LootCountMax;
    LootCountMax = FMath::Max(0, Settings->LootCountMax);
    WFLOG_INFO("[配置] %s 战利品数量上限被 ini 覆盖：类默认值 %d -> 生效 %d"
               "（ini=%d）。",
               *GetName(), OldMax, LootCountMax, Settings->LootCountMax);
  } else {
    WFLOG_INFO("[配置] %s 战利品数量上限没有 ini 覆盖，用类默认值 %d。",
               *GetName(), LootCountMax);
  }

  // 类默认值也可能被蓝图改成负数（编辑器 Clamp 不是运行时保证）。
  Capacity = FMath::Max(1, Capacity);
  LootCountMin = FMath::Max(0, LootCountMin);
  LootCountMax = FMath::Max(0, LootCountMax);

  if (LootCountMax < LootCountMin) {
    WFLOG_WARNING("[配置] %s 的战利品数量下限 %d 大于上限 %d，已把上限抬到下限。",
                  *GetName(), LootCountMin, LootCountMax);
    LootCountMax = LootCountMin;
  }

  WFLOG_INFO("[配置] %s 生效值：容量 %d 格，战利品 %d~%d 件，权限模式=%d。",
             *GetName(), Capacity, LootCountMin, LootCountMax,
             static_cast<int32>(AccessMode));
}

void AChestActor::GenerateLoot() {
  if (!HasAuthority()) {
    WFLOG_ERROR("[宝箱] %s 在非权威端调用 GenerateLoot，已忽略。", *GetName());
    return;
  }
  if (!Container) {
    WFLOG_ERROR("[宝箱] %s 没有 Container，无法生成战利品。", *GetName());
    return;
  }

  UItemDatabaseSubsystem *Database = UItemDatabaseSubsystem::Get(this);
  if (!Database) {
    WFLOG_ERROR("[宝箱] %s 取不到 UItemDatabaseSubsystem，无法生成战利品"
                "（检查 Project Settings > Game > Item System 的 ItemTable）。",
                *GetName());
    return;
  }

  TArray<int32> AllItemIDs;
  Database->GetAllItemIDs(AllItemIDs);
  if (AllItemIDs.Num() == 0) {
    WFLOG_WARNING("[宝箱] %s 的物品数据库为空，无法生成战利品。", *GetName());
    return;
  }

  // 先筛出这一档允许的稀有度，再洗牌随机挑。筛选走 ItemID 而不是直接持有
  // FItemInformation：AddItemByID 内部会再查一次表，定义始终以数据库为准。
  TArray<int32> Candidates;
  Candidates.Reserve(AllItemIDs.Num());
  for (int32 ItemID : AllItemIDs) {
    if (ItemID <= 0) {
      continue;
    }
    const FItemInformation *Definition = Database->GetItemDefinition(ItemID);
    if (Definition &&
        IsRaritySuitableForChestLevel(ChestLevel, Definition->ItemRarity)) {
      Candidates.Add(ItemID);
    }
  }

  if (Candidates.Num() == 0) {
    WFLOG_WARNING("[宝箱] %s 找不到等级 %d 可用的物品"
                  "（检查 DT_Items 的 ItemRarity 是否覆盖该稀有度池）。",
                  *GetName(), static_cast<int32>(ChestLevel));
    return;
  }

  // Fisher-Yates 洗牌：保证同一箱里不会重复抽到同一个 ItemID。
  for (int32 Index = Candidates.Num() - 1; Index > 0; --Index) {
    const int32 SwapIndex = FMath::RandRange(0, Index);
    Candidates.Swap(Index, SwapIndex);
  }

  const int32 MinCount =
      FMath::Max(0, FMath::Min(LootCountMin, LootCountMax));
  const int32 MaxCount =
      FMath::Max(MinCount, FMath::Max(LootCountMin, LootCountMax));
  const int32 DesiredCount = FMath::RandRange(MinCount, MaxCount);
  if (DesiredCount <= 0) {
    WFLOG_INFO("[宝箱] %s 的战利品数量配置为 0，不生成物品。", *GetName());
    return;
  }

  const int32 CapacityLeft =
      FMath::Max(0, Container->GetCapacity() - Container->GetItemCount());
  const int32 Count = FMath::Min(
      FMath::Min(DesiredCount, Candidates.Num()), CapacityLeft);
  if (Count < DesiredCount) {
    WFLOG_WARNING("[宝箱] %s 期望生成 %d 件战利品，实际只能放 %d 件"
                  "（候选物品 %d 件，空位 %d 格）。",
                  *GetName(), DesiredCount, Count, Candidates.Num(),
                  CapacityLeft);
  }

  int32 AddedCount = 0;
  for (int32 Index = 0; Index < Count; ++Index) {
    const int32 ItemID = Candidates[Index];
    const int32 Added = Container->AddItemByID(ItemID, 1);
    if (Added <= 0) {
      WFLOG_WARNING("[宝箱] %s 放入 ItemID=%d 失败（容器是否已满？）。",
                    *GetName(), ItemID);
      continue;
    }

    ++AddedCount;
    const FItemInformation *Definition = Database->GetItemDefinition(ItemID);
    WFLOG_INFO("[宝箱] %s 生成战利品：ItemID=%d，名称=%s，稀有度=%d，数量=%d。",
               *GetName(), ItemID,
               Definition ? *Definition->ItemName.ToString() : TEXT("未知"),
               Definition ? static_cast<int32>(Definition->ItemRarity) : -1,
               Added);
  }

  WFLOG_INFO("[宝箱] %s 战利品生成完成：等级 %d，候选 %d 件，期望 %d 件，"
             "实际放入 %d 件。",
             *GetName(), static_cast<int32>(ChestLevel), Candidates.Num(),
             DesiredCount, AddedCount);
}

void AChestActor::SetChestLevel(EChestLevel InChestLevel) {
  if (!HasAuthority()) {
    WFLOG_ERROR("[宝箱] %s 在非权威端调用 SetChestLevel，已忽略；"
                "客户端只认复制下来的等级。",
                *GetName());
    return;
  }

  if (ChestLevel == InChestLevel) {
    WFLOG_INFO("[宝箱] %s 的等级本来就是 %d，忽略设置。", *GetName(),
               static_cast<int32>(ChestLevel));
    return;
  }

  const EChestLevel OldLevel = ChestLevel;
  ChestLevel = InChestLevel;

  // 服务器不会收到自己的 RepNotify：手动补一次，让监听服务器也刷新表现。
  OnRep_ChestLevel();

  WFLOG_INFO("[宝箱] %s 等级 %d -> %d（只改表现/后续掉落规则，"
             "不会重抽已经生成的战利品）。",
             *GetName(), static_cast<int32>(OldLevel),
             static_cast<int32>(ChestLevel));
}

void AChestActor::OnRep_ChestLevel() {
  WFLOG_INFO("[宝箱] %s 应用等级 %d，调用蓝图 BP_OnChestLevelChanged。",
             *GetName(), static_cast<int32>(ChestLevel));
  BP_OnChestLevelChanged(ChestLevel);
}

void AChestActor::OnRep_bIsOpened() {
  WFLOG_INFO("[宝箱] %s 打开状态 -> %s，调用蓝图 %s。", *GetName(),
             bIsOpened ? TEXT("打开") : TEXT("关闭"),
             bIsOpened ? TEXT("BP_OnChestOpened") : TEXT("BP_OnChestClosed"));

  if (bIsOpened) {
    BP_OnChestOpened();
  } else {
    BP_OnChestClosed();
  }
}

void AChestActor::SetOpenedState(bool bNewOpened) {
  if (bIsOpened == bNewOpened) {
    return;
  }

  bIsOpened = bNewOpened;

  // 服务器不会收到自己的 RepNotify：手动补一次，让监听服务器也播放表现。
  // 客户端由引擎在属性复制后调用 OnRep_bIsOpened。
  OnRep_bIsOpened();
}

bool AChestActor::OpenChest(APlayerController *Player) {
  if (!HasAuthority()) {
    WFLOG_ERROR("[宝箱] %s 在非权威端调用 OpenChest，已忽略。", *GetName());
    return false;
  }
  if (!Player) {
    WFLOG_WARNING("[宝箱] %s 打开请求被拒：Player 为空。", *GetName());
    return false;
  }
  if (!Container) {
    WFLOG_ERROR("[宝箱] %s 没有 Container，无法打开。", *GetName());
    return false;
  }
  if (Container->GetCapacity() <= 0) {
    WFLOG_ERROR("[宝箱] %s 的容器尚未初始化（容量 %d），拒绝打开。",
                *GetName(), Container->GetCapacity());
    return false;
  }
  if (!Container->HasAccess(Player)) {
    WFLOG_WARNING("[宝箱] 玩家 %s 无权打开宝箱 %s（权限模式=%d）。",
                  *GetNameSafe(Player), *GetName(),
                  static_cast<int32>(Container->GetAccessMode()));
    return false;
  }

  // 没有窗口类就没有 UI：此时允许打开会让玩家进入「有活动容器但没有窗口」的
  // 卡死状态，所以在服务器入口直接拒绝。
  if (!ChestWindowClass) {
    WFLOG_ERROR("[宝箱] %s 没有指定 ChestWindowClass，拒绝打开。"
                "请在宝箱蓝图类默认值里选择一个继承 UChestWindowWidget 的 WBP。",
                *GetName());
    return false;
  }

  CleanupActiveUsers();
  const bool bFirstOpener = ActiveUsers.IsEmpty();
  ActiveUsers.Add(TWeakObjectPtr<APlayerController>(Player));

  if (bFirstOpener) {
    SetOpenedState(true);
  }

  WFLOG_INFO("[宝箱] 玩家 %s 打开宝箱 %s（当前打开者 %d 人，%s）。",
             *GetNameSafe(Player), *GetName(), ActiveUsers.Num(),
             bFirstOpener ? TEXT("播放开箱表现")
                          : TEXT("已有其它玩家打开，不重复播放"));

  return true;
}

bool AChestActor::CloseChest(APlayerController *Player) {
  if (!HasAuthority()) {
    WFLOG_ERROR("[宝箱] %s 在非权威端调用 CloseChest，已忽略。", *GetName());
    return false;
  }

  CleanupActiveUsers();

  if (!Player) {
    const int32 RemovedCount = ActiveUsers.Num();
    ActiveUsers.Reset();
    SetOpenedState(false);
    WFLOG_INFO("[宝箱] %s 被强制关闭（清理 %d 名打开者）。", *GetName(),
               RemovedCount);
    return true;
  }

  const int32 Removed =
      ActiveUsers.Remove(TWeakObjectPtr<APlayerController>(Player));
  CleanupActiveUsers();

  WFLOG_INFO("[宝箱] 玩家 %s 离开宝箱 %s（移除=%d，剩余打开者 %d 人）。",
             *GetNameSafe(Player), *GetName(), Removed, ActiveUsers.Num());

  // 最后一名打开者离开时才真正关箱；否则视觉上会替其它仍在查看的玩家关掉。
  if (ActiveUsers.IsEmpty()) {
    SetOpenedState(false);
  }

  return true;
}

void AChestActor::CleanupActiveUsers() {
  for (auto It = ActiveUsers.CreateIterator(); It; ++It) {
    if (!(*It).IsValid()) {
      It.RemoveCurrent();
    }
  }
}
