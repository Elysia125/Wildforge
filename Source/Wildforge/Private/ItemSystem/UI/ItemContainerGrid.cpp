#include "ItemSystem/UI/ItemContainerGrid.h"

#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "GameFramework/Pawn.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/UI/InventorySlotWidget.h"
#include "Utils/WildforgeLog.h"

void UItemContainerGrid::NativeConstruct() {
  Super::NativeConstruct();

  // 【诊断】控件实例化时按 WBP 模板逐属性拷贝，Container 这类普通对象引用即使标了
  // Transient 也会被带过来（UObjectGlobals.cpp:4671-4684）——进入本函数时它可能指向
  // 一个早已销毁的对象。弱引用对失效目标只报「无效」，这里记录一条以便定位来源。
  if (Container.IsStale(true)) {
    WFLOG_WARNING("ItemContainerGrid::NativeConstruct: 检测到继承自控件模板的失效容器引用，"
                  "已丢弃（弱指针，未解引用）");
  }

  // 不再用 if (!Container) 判断 —— 「非空」不等于「有效」：继承来的死指针同样非空，
  // 旧写法会因此跳过重解析，把垃圾指针直接喂给 RefreshGrid（bug-064）。
  // 一律以「拥有者 Pawn 的实时查找结果」为准
  // （容器是 APlayerCharacter 上的 UPlayerInventory 组件，不在 PlayerController 上）。
  // if (APawn *Pawn = GetOwningPlayerPawn()) {
  //   if (UItemContainer *FoundContainer =
  //           Pawn->FindComponentByClass<UItemContainer>()) {
  //     InitializeGrid(FoundContainer, SlotsPerRow);
  //     return; // InitializeGrid 内部已重绑 + RefreshGrid
  //   }
  // }

  // 拿不到 Pawn/容器：若手上还留着一个有效引用（例如父级已先调过 InitializeGrid），保持绑定
  BindToContainer();
}

void UItemContainerGrid::NativeDestruct() {
  UnbindFromContainer();

  Super::NativeDestruct();
}

void UItemContainerGrid::HandleContainerChanged() {
  // 只处理「结构」：容量可能变了（ResizeContainer 也走这个广播），需要增删槽位控件。
  // 「内容」不在这里刷——每个槽位都订阅了同一个委托，会自己按 (容器, 索引) 拉数据，
  // 并且只在自己那一格真的变了时才碰 Slate，所以这里不要整表刷。
  if (!MainUniformGridPanel) {
    return;
  }

  UItemContainer *LiveContainer = Container.Get();
  const int32 Capacity =
      (LiveContainer && SlotWidgetClass) ? LiveContainer->GetCapacity() : 0;

  // 只有数量真的变了才重排：LayoutSlots 会写每个槽位的 Row/Column 并让布局失效，
  // 每次广播都写一遍就把「按需刷新」的收益又还回去了。
  if (EnsureSlotCount(Capacity)) {
    LayoutSlots();
  }
}

void UItemContainerGrid::BindToContainer() {
  // 先解绑，避免重复订阅
  UnbindFromContainer();

  // 经弱引用取活对象：目标已被回收时这里是 nullptr，不会再对死指针操作委托
  if (UItemContainer *LiveContainer = Container.Get()) {
    LiveContainer->OnContainerChanged.AddDynamic(
        this, &UItemContainerGrid::HandleContainerChanged);
  }
}

void UItemContainerGrid::UnbindFromContainer() {
  if (UItemContainer *LiveContainer = Container.Get()) {
    LiveContainer->OnContainerChanged.RemoveDynamic(
        this, &UItemContainerGrid::HandleContainerChanged);
  }
}

void UItemContainerGrid::InitializeGrid(UItemContainer *InContainer,
                                        int32 InSlotsPerRow) {
  if (Container.Get() != InContainer) {
    UnbindFromContainer();
    Container = InContainer;
  }
  // InSlotsPerRow <= 0 = 不覆盖，沿用控件上配置的 SlotsPerRow（WBP 设计器里配的每行个数；
  // 调用方硬编码一个数字会把它悄悄冲掉）。统一夹到 >= 1：LayoutSlots 拿它当除数。
  SlotsPerRow = FMath::Max(1, InSlotsPerRow > 0 ? InSlotsPerRow : SlotsPerRow);

  // 日志也走弱引用解析后的对象：传入的裸指针一律不直接解引用
  UItemContainer *LiveContainer = Container.Get();
  WFLOG_INFO("ItemContainerGrid::InitializeGrid: 绑定容器 %s（容量 %d），每行 %d 个",
             *GetNameSafe(LiveContainer),
             LiveContainer ? LiveContainer->GetCapacity() : 0, SlotsPerRow);

  BindToContainer();
  RefreshGrid();
}

void UItemContainerGrid::SetSlotsPerRow(int32 InSlotsPerRow) {
  SlotsPerRow = FMath::Max(1, InSlotsPerRow);
  RefreshGrid();
}

void UItemContainerGrid::RefreshGrid() {
  if (!MainUniformGridPanel) {
    return;
  }

  // 目标槽位数 = 容器容量（经弱引用取活对象；容器已销毁/未指定槽位类时为 0）。
  // 取不到容器时容量按 0 处理 = 不建任何槽位——绝不让「失效指针读到垃圾容量」
  // 变成「按垃圾数量创建一堆槽位控件」（bug-064）。
  UItemContainer *LiveContainer = Container.Get();
  const int32 Capacity =
      (LiveContainer && SlotWidgetClass) ? LiveContainer->GetCapacity() : 0;

  // 全量路径（初始化 / 换容器 / 改每行个数）：保证数量与布局，
  // 再让每个槽位重新绑定来源并拉一次数据——重复绑到同一来源只会补一次拉取。
  EnsureSlotCount(Capacity);
  LayoutSlots();

  for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index) {
    if (UInventorySlotWidget *SlotWidget = SlotWidgets[Index]) {
      SlotWidget->InitializeSlot(LiveContainer, Index);
    }
  }
}

bool UItemContainerGrid::EnsureSlotCount(int32 DesiredCount) {
  if (!MainUniformGridPanel) {
    return false;
  }

  // 容量理论上来自容器（玩家背包 30 上下）。一旦读到异常大的值，说明上游数据已经坏了：
  // 宁可夹紧，也不能照着垃圾值创建成百上千个控件（bug-064 的推手之一）。
  constexpr int32 MaxReasonableSlots = 512;
  if (DesiredCount < 0 || DesiredCount > MaxReasonableSlots) {
    WFLOG_ERROR("ItemContainerGrid::EnsureSlotCount: 目标槽位数 %d 超出合理范围 [0, %d]，"
                "已夹紧（容器或容量数据可能已损坏）",
                DesiredCount, MaxReasonableSlots);
  }
  DesiredCount = FMath::Clamp(DesiredCount, 0, MaxReasonableSlots);
  bool bChanged = false;

  // 容量缩小：从尾部移除多余槽位
  while (SlotWidgets.Num() > DesiredCount) {
    UInventorySlotWidget *Extra = SlotWidgets.Pop(EAllowShrinking::No);
    if (Extra) {
      // 显式退订：控件要等 Slate 资源释放才走 NativeDestruct，
      // 别让容器在这中间继续挂着一个即将销毁的槽位
      Extra->UnbindFromContainer();
      Extra->RemoveFromParent();
    }
    bChanged = true;
  }

  // 容量增大：补齐空槽（位置随后由 LayoutSlots 统一设置）
  while (SlotWidgets.Num() < DesiredCount) {
    if (!SlotWidgetClass) {
      break;
    }
    UInventorySlotWidget *SlotWidget =
        CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), SlotWidgetClass);
    if (!SlotWidget) {
      break;
    }
    const int32 NewIndex = SlotWidgets.Num();
    MainUniformGridPanel->AddChildToUniformGrid(SlotWidget, 0, 0);
    // 新槽位当场绑定来源并拉一次数据：本次广播的调用列表在广播前已经拷贝过
    // （ScriptDelegates.h:924-926），这一轮它自己收不到通知。
    // 来源取弱引用解析后的活对象：容器已销毁时传 nullptr，槽位按空槽展示
    SlotWidget->InitializeSlot(Container.Get(), NewIndex);
    SlotWidgets.Add(SlotWidget);
    bChanged = true;
  }

  return bChanged;
}

void UItemContainerGrid::LayoutSlots() {
  if (!MainUniformGridPanel) {
    return;
  }

  for (int32 Index = 0; Index < SlotWidgets.Num(); ++Index) {
    UInventorySlotWidget *SlotWidget = SlotWidgets[Index];
    if (!SlotWidget) {
      continue;
    }
    if (UUniformGridSlot *GridSlot = Cast<UUniformGridSlot>(SlotWidget->Slot)) {
      GridSlot->SetRow(Index / SlotsPerRow);
      GridSlot->SetColumn(Index % SlotsPerRow);
    }
  }
}