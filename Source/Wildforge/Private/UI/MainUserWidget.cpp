// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/MainUserWidget.h"

#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ItemSystem/Components/QuickContainer.h"
#include "Utils/WildforgeLog.h"

// 快捷栏初始化的**时序**（读这段之前先看 APlayerCharacter::BeginPlay 的背包初始化注释）：
//
//   1) 本控件（W_MainWidget）是在 BP_FirstPersonPlayerController 的 BeginPlay 里创建并
//      AddToViewport 的；容器的容量却是 APlayerCharacter::BeginPlay 里
//      InitializeContainer 写进 Slots 的。两者在同一个 DispatchBeginPlay 批次里，
//      **PlayerController 排在 Pawn 前面** —— 所以 NativeConstruct 跑的时候，
//      FindComponentByClass 能拿到活着的 QuickContainer，但它的 Slots 还是空的，
//      GetCapacity()（= Slots.Num()）就是 0。（实证：Saved/Logs/Wildforge/*/Info.log 里
//      "ItemContainerGrid::InitializeGrid: 绑定容器 …" 稳定出现在
//      "[背包] … 初始化背包容器：容量 N 格" 之前 ~20ms。此前容量是在角色**构造函数**里
//      初始化的，构造函数在 spawn 时就跑，早于控制器 BeginPlay，所以从来没人踩到这个坑。）
//   2) 客户端还要再晚一步：Slots 是 COND_OwnerOnly 复制过来的，
//      客户端在 OnRep_Slots 里 RebuildDerivedState 之后才有容量；而且本地 PlayerController
//      的 BeginPlay 常常早于「Pawn 复制到位并被占有」，那一刻连 Pawn 都还没有。
//
// 结论：这里**不能**把「读到容量」当作初始化的前提，也不能拿容量当「每行几个」传下去
// （容量 0 时 UItemContainerGrid::InitializeGrid 会退回控件上配的 SlotsPerRow，
// 把单行快捷栏排成多行）。正确做法是把容器交出去就完事：
// 容器容量到位时自己会广播 OnContainerChanged，由
// UQuickBarWidget::HandleContainerChanged 补齐「每行个数 = 容量」、槽位控件与按键标签。

void UMainUserWidget::NativeConstruct() {
  Super::NativeConstruct();

  if (!QuickBarWidget) {
    WFLOG_WARNING("UMainUserWidget::NativeConstruct: 没有绑定 QuickBarWidget"
                  "（UMG 里的名字必须正好是 QuickBarWidget）");
    return;
  }

  // 现在试一次（本端容器可能已经初始化过），再挂上「占有变化」这一枪补客户端。
  TryInitializeQuickBar();

  if (APlayerController *PC = GetOwningPlayer()) {
    // 先退订再订阅：控件被复用/重建时不留下重复绑定
    PC->OnPossessedPawnChanged.RemoveDynamic(
        this, &UMainUserWidget::HandlePossessedPawnChanged);
    PC->OnPossessedPawnChanged.AddDynamic(
        this, &UMainUserWidget::HandlePossessedPawnChanged);
  } else {
    WFLOG_WARNING("UMainUserWidget::NativeConstruct: 拿不到 OwningPlayer，"
                  "快捷栏不会补绑（只能在 NativeConstruct 这一次尝试）");
  }
}

void UMainUserWidget::NativeDestruct() {
  if (APlayerController *PC = GetOwningPlayer()) {
    PC->OnPossessedPawnChanged.RemoveDynamic(
        this, &UMainUserWidget::HandlePossessedPawnChanged);
  }

  Super::NativeDestruct();
}

void UMainUserWidget::HandlePossessedPawnChanged(APawn *OldPawn, APawn *NewPawn) {
  WFLOG_INFO("UMainUserWidget: 占有关系变化（%s -> %s），重试初始化快捷栏",
             *GetNameSafe(OldPawn), *GetNameSafe(NewPawn));
  TryInitializeQuickBar();
}

void UMainUserWidget::TryInitializeQuickBar() {
  APawn *Pawn = GetOwningPlayerPawn();
  if (!Pawn) {
    WFLOG_INFO("UMainUserWidget: 还没有 Pawn（客户端通常要等 Pawn 复制到位），"
               "快捷栏延后初始化（等 OnPossessedPawnChanged）");
    return;
  }

  UQuickContainer *FoundContainer = Pawn->FindComponentByClass<UQuickContainer>();
  if (!FoundContainer) {
    WFLOG_WARNING("UMainUserWidget: %s 上没有 UQuickContainer，快捷栏不可用"
                  "（组件是不是被改名/删掉了？）",
                  *Pawn->GetName());
    return;
  }

  // InSlotsPerRow 传容量只是「容量已经知道时正好是单行」的顺带写法；
  // 真正生效的是容器广播时 HandleContainerChanged 里的校正，见文件头注释。
  QuickBarWidget->InitializeGrid(FoundContainer, FoundContainer->GetCapacity());
  WFLOG_INFO("UMainUserWidget: 快捷栏已绑定容器 %s（绑定瞬间容量 %d；容量到位时容器会"
             "自己广播 OnContainerChanged 补齐槽位与按键标签）",
             *FoundContainer->GetName(), FoundContainer->GetCapacity());
}

void UMainUserWidget::ShowInventory_Implementation() {
  if (InventoryWidget) {
    bIsInventoryOpen = !bIsInventoryOpen;
    InventoryWidget->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Visible
                                                    : ESlateVisibility::Hidden);
  }
}
