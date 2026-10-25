// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/MainUserWidget.h"

#include "Character/Components/CharacterAttributes.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
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
//
// 属性 HUD（血条/精力条/经验条）走同一套时序：NativeConstruct 先按当前值刷一次，
// 客户端等 OnPossessedPawnChanged 补绑；数值本身由服务器复制，UI 只读。

void UMainUserWidget::NativeConstruct() {
  Super::NativeConstruct();

  // 设计器里的 bar 槽位尺寸只在这一刻是「原始值」（之后会被上限拉伸改写），
  // 先抓下来做基准；控件被重复 Construct 时不会覆盖已缓存的值。
  CacheAttributeBarBaseSizes();

  if (!QuickBarWidget) {
    WFLOG_WARNING("UMainUserWidget::NativeConstruct: 没有绑定 QuickBarWidget"
                  "（UMG 里的名字必须正好是 QuickBarWidget）");
  } else {
    // 现在试一次（本端容器可能已经初始化过），再挂上「占有变化」这一枪补客户端。
    TryInitializeQuickBar();
  }

  if (APlayerController *PC = GetOwningPlayer()) {
    // 先退订再订阅：控件被复用/重建时不留下重复绑定
    PC->OnPossessedPawnChanged.RemoveDynamic(
        this, &UMainUserWidget::HandlePossessedPawnChanged);
    PC->OnPossessedPawnChanged.AddDynamic(
        this, &UMainUserWidget::HandlePossessedPawnChanged);
  } else {
    WFLOG_WARNING("UMainUserWidget::NativeConstruct: 拿不到 OwningPlayer，"
                  "快捷栏与属性 HUD 都不会补绑（只能在 NativeConstruct 这一次尝试）");
  }

  // 属性条与快捷栏互相独立：快捷栏缺了也不能阻止血条绑定。
  if (!HPProgressBar || !EnergyProgressBar || !ExperienceProgressBar) {
    WFLOG_WARNING(
        "UMainUserWidget: 属性进度条绑定不完整（HP=%d 精力=%d 经验=%d）。"
        "请在 W_MainWidget 里检查控件名是否正好是 HPProgressBar / "
        "EnergyProgressBar / ExperienceProgressBar。",
        HPProgressBar ? 1 : 0, EnergyProgressBar ? 1 : 0,
        ExperienceProgressBar ? 1 : 0);
  }
  if (!LevelText) {
    WFLOG_INFO("UMainUserWidget: 没有绑定 LevelText（资产里当前叫 TextBlock_53）。"
               "把该文本控件重命名为 LevelText 后，等级会自动显示为 Lv.N。");
  }

  TryInitializeAttributes();
}

void UMainUserWidget::NativeDestruct() {
  UnbindAttributes();

  if (APlayerController *PC = GetOwningPlayer()) {
    PC->OnPossessedPawnChanged.RemoveDynamic(
        this, &UMainUserWidget::HandlePossessedPawnChanged);
  }

  Super::NativeDestruct();
}

void UMainUserWidget::HandlePossessedPawnChanged(APawn *OldPawn, APawn *NewPawn) {
  WFLOG_INFO("UMainUserWidget: 占有关系变化（%s -> %s），重试初始化快捷栏与属性",
             *GetNameSafe(OldPawn), *GetNameSafe(NewPawn));
  TryInitializeQuickBar();
  TryInitializeAttributes();
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

void UMainUserWidget::TryInitializeAttributes() {
  APawn *Pawn = GetOwningPlayerPawn();
  if (!Pawn) {
    WFLOG_INFO("UMainUserWidget: 属性绑定等待 Pawn（客户端通常要等 Pawn 复制到位），"
               "等 OnPossessedPawnChanged 再试");
    return;
  }

  UCharacterAttributes *Attributes =
      Pawn->FindComponentByClass<UCharacterAttributes>();
  if (!Attributes) {
    WFLOG_WARNING("UMainUserWidget: %s 上没有 UCharacterAttributes，"
                  "生命/精力/经验 HUD 不可用",
                  *Pawn->GetName());
    return;
  }

  if (BoundAttributes.Get() == Attributes) {
    // 同一个 Pawn 的重复回调：只刷新一次，不重复订阅。
    RefreshAttributeWidgets();
    return;
  }

  UnbindAttributes();
  BoundAttributes = Attributes;

  Attributes->OnHealthChanged.AddDynamic(
      this, &UMainUserWidget::HandleHealthChanged);
  Attributes->OnMaxHealthChanged.AddDynamic(
      this, &UMainUserWidget::HandleMaxHealthChanged);
  Attributes->OnEnergyChanged.AddDynamic(
      this, &UMainUserWidget::HandleEnergyChanged);
  Attributes->OnMaxEnergyChanged.AddDynamic(
      this, &UMainUserWidget::HandleMaxEnergyChanged);
  Attributes->OnLevelChanged.AddDynamic(this,
                                        &UMainUserWidget::HandleLevelChanged);
  Attributes->OnExperienceChanged.AddDynamic(
      this, &UMainUserWidget::HandleExperienceChanged);

  // 立即按当前值刷一次：服务器端此时已经 BeginPlay 过；客户端可能还是默认值，
  // 等复制/OnRep 到达后会再次刷新。
  RefreshAttributeWidgets();

  WFLOG_INFO(
      "UMainUserWidget: 已绑定属性组件 %s（等级=%d 生命=%.1f/%.1f "
      "精力=%.1f/%.1f 经验=%d/%d）",
      *Attributes->GetName(), Attributes->GetLevel(),
      Attributes->GetHealth(), Attributes->GetMaxHealth(),
      Attributes->GetEnergy(), Attributes->GetMaxEnergy(),
      Attributes->GetExperience(), Attributes->GetExperienceToNextLevel());
}

void UMainUserWidget::UnbindAttributes() {
  if (UCharacterAttributes *Attributes = BoundAttributes.Get()) {
    Attributes->OnHealthChanged.RemoveDynamic(
        this, &UMainUserWidget::HandleHealthChanged);
    Attributes->OnMaxHealthChanged.RemoveDynamic(
        this, &UMainUserWidget::HandleMaxHealthChanged);
    Attributes->OnEnergyChanged.RemoveDynamic(
        this, &UMainUserWidget::HandleEnergyChanged);
    Attributes->OnMaxEnergyChanged.RemoveDynamic(
        this, &UMainUserWidget::HandleMaxEnergyChanged);
    Attributes->OnLevelChanged.RemoveDynamic(
        this, &UMainUserWidget::HandleLevelChanged);
    Attributes->OnExperienceChanged.RemoveDynamic(
        this, &UMainUserWidget::HandleExperienceChanged);
  }
  BoundAttributes.Reset();
}

void UMainUserWidget::RefreshAttributeWidgets() {
  UCharacterAttributes *Attributes = BoundAttributes.Get();

  // 先按上限调整 bar 长度（内部会处理「没绑定属性 / 关闭了拉伸」两种情况）。
  UpdateAttributeBarLengths();

  if (!Attributes) {
    return;
  }

  if (HPProgressBar) {
    HPProgressBar->SetPercent(Attributes->GetHealthPercent());
  }
  if (EnergyProgressBar) {
    EnergyProgressBar->SetPercent(Attributes->GetEnergyPercent());
  }
  if (ExperienceProgressBar) {
    ExperienceProgressBar->SetPercent(Attributes->GetExperiencePercent());
  }
  if (LevelText) {
    LevelText->SetText(
        FText::FromString(FString::Printf(TEXT("Lv.%d"), Attributes->GetLevel())));
  }
}

void UMainUserWidget::CacheAttributeBarBaseSizes() {
  // 每条 bar 独立缓存：某一条不在 CanvasPanel 下时不拖累另一条。
  if (BaseHPBarSize.IsZero() && HPProgressBar) {
    if (const UCanvasPanelSlot *CanvasSlot =
            Cast<UCanvasPanelSlot>(HPProgressBar->Slot)) {
      BaseHPBarSize = CanvasSlot->GetSize();
    } else {
      WFLOG_WARNING(
          "UMainUserWidget: HPProgressBar 的槽位不是 UCanvasPanelSlot，"
          "无法随生命上限拉伸长度（请把它直接放在 CanvasPanel 下）。");
    }
  }

  if (BaseEnergyBarSize.IsZero() && EnergyProgressBar) {
    if (const UCanvasPanelSlot *CanvasSlot =
            Cast<UCanvasPanelSlot>(EnergyProgressBar->Slot)) {
      BaseEnergyBarSize = CanvasSlot->GetSize();
    } else {
      WFLOG_WARNING(
          "UMainUserWidget: EnergyProgressBar 的槽位不是 UCanvasPanelSlot，"
          "无法随精力上限拉伸长度（请把它直接放在 CanvasPanel 下）。");
    }
  }

  WFLOG_INFO("UMainUserWidget: bar 基准尺寸 HP=%.1f×%.1f 精力=%.1f×%.1f"
             "（按上限倍率拉伸，最大 %.2fx）",
             BaseHPBarSize.X, BaseHPBarSize.Y, BaseEnergyBarSize.X,
             BaseEnergyBarSize.Y, MaxBarStretch);
}

void UMainUserWidget::ResizeAttributeBar(UWidget *Bar, float Width,
                                         float Height) {
  if (!Bar) {
    return;
  }

  // 注意：局部变量不能叫 Slot——UMainUserWidget 继承自 UWidget，而 UWidget
  // 自己就有一个 Slot 成员，同名会触发 C4458（本项目按错误处理）。
  UCanvasPanelSlot *CanvasSlot = Cast<UCanvasPanelSlot>(Bar->Slot);
  if (!CanvasSlot) {
    // 缓存阶段已经报过警告；这里静默跳过，避免每次刷新都刷屏。
    return;
  }

  // Auto Size 打开时 SConstraintCanvas 会忽略手动尺寸，必须先关掉。
  if (CanvasSlot->GetAutoSize()) {
    CanvasSlot->SetAutoSize(false);
  }

  const FVector2D NewSize(FMath::Max(1.0f, Width), FMath::Max(1.0f, Height));
  if (!CanvasSlot->GetSize().Equals(NewSize)) {
    CanvasSlot->SetSize(NewSize);
  }
}

void UMainUserWidget::UpdateAttributeBarLengths() {
  // 关闭拉伸时恢复设计尺寸，避免运行中切换开关后残留上一次的拉伸。
  if (!bStretchBarsWithMaxValue) {
    if (!BaseHPBarSize.IsZero()) {
      ResizeAttributeBar(HPProgressBar, BaseHPBarSize.X, BaseHPBarSize.Y);
    }
    if (!BaseEnergyBarSize.IsZero()) {
      ResizeAttributeBar(EnergyProgressBar, BaseEnergyBarSize.X,
                         BaseEnergyBarSize.Y);
    }
    return;
  }

  UCharacterAttributes *Attributes = BoundAttributes.Get();
  if (!Attributes) {
    return;
    
  }

  // 1 级上限就是曲线起点。这两个值来自服务器下发的成长参数（COND_InitialOnly），
  // 客户端用同一份参数计算，不读本地 ini。
  const float BaseMaxHealth =
      FMath::Max(1.0f, Attributes->GetMaxHealthForLevel(1));
  const float BaseMaxEnergy =
      FMath::Max(1.0f, Attributes->GetMaxEnergyForLevel(1));
  const float StretchCap = FMath::Max(1.0f, MaxBarStretch);

  // 长度倍率 = 当前上限 / 1 级上限；只拉长不缩短（下限 1），并夹到上限。
  const float HealthStretch = FMath::Clamp(
      Attributes->GetMaxHealth() / BaseMaxHealth, 1.0f, StretchCap);
  const float EnergyStretch = FMath::Clamp(
      Attributes->GetMaxEnergy() / BaseMaxEnergy, 1.0f, StretchCap);

  if (!BaseHPBarSize.IsZero()) {
    ResizeAttributeBar(HPProgressBar, BaseHPBarSize.X * HealthStretch,
                       BaseHPBarSize.Y);
  }
  if (!BaseEnergyBarSize.IsZero()) {
    ResizeAttributeBar(EnergyProgressBar, BaseEnergyBarSize.X * EnergyStretch,
                       BaseEnergyBarSize.Y);
  }

  // 只在倍率真的变化时记日志：掉血/回血会高频刷新，但倍率只看上限。
  if (FMath::Abs(HealthStretch - LastAppliedHPBarStretch) > 0.001f ||
      FMath::Abs(EnergyStretch - LastAppliedEnergyBarStretch) > 0.001f) {
    LastAppliedHPBarStretch = HealthStretch;
    LastAppliedEnergyBarStretch = EnergyStretch;
    WFLOG_INFO(
        "UMainUserWidget: bar 拉伸更新 生命=%.2fx（上限 %.1f / 基准 %.1f，"
        "宽度 %.1f） 精力=%.2fx（上限 %.1f / 基准 %.1f，宽度 %.1f）",
        HealthStretch, Attributes->GetMaxHealth(), BaseMaxHealth,
        BaseHPBarSize.X * HealthStretch, EnergyStretch,
        Attributes->GetMaxEnergy(), BaseMaxEnergy,
        BaseEnergyBarSize.X * EnergyStretch);
  }
}

void UMainUserWidget::HandleHealthChanged(float NewHealth) {
  RefreshAttributeWidgets();
}

void UMainUserWidget::HandleMaxHealthChanged(float NewMaxHealth) {
  RefreshAttributeWidgets();
}

void UMainUserWidget::HandleEnergyChanged(float NewEnergy) {
  RefreshAttributeWidgets();
}

void UMainUserWidget::HandleMaxEnergyChanged(float NewMaxEnergy) {
  RefreshAttributeWidgets();
}

void UMainUserWidget::HandleLevelChanged(int32 NewLevel) {
  RefreshAttributeWidgets();
}

void UMainUserWidget::HandleExperienceChanged(int32 NewExperience,
                                              int32 NewExperienceToNextLevel) {
  RefreshAttributeWidgets();
}

void UMainUserWidget::ShowInventory_Implementation() {
  if (InventoryWidget) {
    bIsInventoryOpen = !bIsInventoryOpen;
    InventoryWidget->SetVisibility(bIsInventoryOpen ? ESlateVisibility::Visible
                                                    : ESlateVisibility::Hidden);
  }
}
