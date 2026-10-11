#include "ItemSystem/UI/QuickBarWidget.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/UI/InventorySlotWidget.h"
#include "ItemSystem/Structs/ItemInfo.h"
#include "ItemSystem/Structs/QuickSlotContext.h"
#include "Utils/HotkeyUtils.h"
#include "Utils/WildforgeLog.h"

void UQuickBarWidget::InitializeGrid(UItemContainer *InContainer,
                                     int32 InSlotsPerRow) {
  Super::InitializeGrid(InContainer, InSlotsPerRow);

  // 快捷键绑定**只用到 HotkeyBindings**，不依赖槽位控件已存在 —— 这一点很关键：
  // 本控件由 BP_FirstPersonPlayerController 的 BeginPlay 创建，那一刻
  // APlayerCharacter::BeginPlay 还没跑（同一个 DispatchBeginPlay 批次里
  // PlayerController 排在 Pawn 之前），容器容量还是 0、一个槽位控件都没有。
  // 槽位控件会在容器广播时补建，标签由 HandleContainerChanged -> RefreshHotkeyLabels 补。
  APlayerController *PC = GetOwningPlayerController();
  SetupHotkeyBindings(PC ? PC->InputComponent : nullptr);
  RefreshHotkeyLabels();
}

void UQuickBarWidget::HandleContainerChanged() {
  // 容量到位之后（权威端 APlayerCharacter::BeginPlay 的 InitializeContainer，
  // 客户端是 OnRep_Slots）才知道「一行几个」：快捷栏是单行，每行个数 == 容量。
  // 必须在 Super 之前改：Super 里 EnsureSlotCount 之后**立刻** LayoutSlots，
  // 改晚了这一轮新槽位就先落在旧列数上，要等下一次广播才纠正。
  if (UItemContainer *LiveContainer = Container.Get()) {
    const int32 Capacity = LiveContainer->GetCapacity();
    if (Capacity > 0 && SlotsPerRow != Capacity) {
      WFLOG_INFO("UQuickBarWidget: 容器容量到位（%d），每行个数 %d -> %d（快捷栏单行）",
                 Capacity, SlotsPerRow, Capacity);
      SlotsPerRow = Capacity;
    }
  }

  Super::HandleContainerChanged(); // 增删槽位控件 + 按 SlotsPerRow 重排
  RefreshHotkeyLabels();           // 槽位控件可能刚建出来，补上按键标签
}

void UQuickBarWidget::RefreshHotkeyLabels() {
  for (int32 i = 0; i < SlotWidgets.Num(); ++i) {
    UInventorySlotWidget *SlotWidget = SlotWidgets[i];
    if (!SlotWidget) {
      continue;
    }

    const FText Desired =
        (i < HotkeyBindings.Num())
            ? FText::FromString(InputChordToString(HotkeyBindings[i].Chord))
            : FText::FromString(TEXT("<None>"));

    // 只在内容真的变了才写：SetButtomExtraText 会写 Slate 并落一条日志，
    // 而本函数每次容器广播（拖拽 / 移动 / 整理物品都会广播）都会被调用一次。
    if (SlotWidget->GetButtomExtraText().EqualTo(Desired)) {
      continue;
    }
    SlotWidget->SetButtomExtraText(Desired);
  }
}
void UQuickBarWidget::SetupHotkeyBindings(
    UInputComponent *PlayerInputComponent) {
  // 1) 必须使用 Enhanced Input 组件
  UEnhancedInputComponent *EIC =
      Cast<UEnhancedInputComponent>(PlayerInputComponent);
  if (!EIC) {
    WFLOG_WARNING(
        "UQuickBarWidget: 需要 Enhanced Input 系统（请检查项目输入设置）");
    return;
  }

  // 2) 获取本地玩家子系统
  APlayerController *PC = GetOwningPlayerController();
  if (!PC || !PC->GetLocalPlayer()) {
    WFLOG_WARNING("UQuickBarWidget: 无法获取 LocalPlayer");
    return;
  }

  UEnhancedInputLocalPlayerSubsystem *Subsystem =
      ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
          PC->GetLocalPlayer());
  if (!Subsystem)
    return;

  // 3) 创建运行时映射上下文
  RuntimeContext =
      NewObject<UInputMappingContext>(this, TEXT("RuntimeHotkeyContext"));
  RuntimeActions.Empty();

  // 4) 遍历配置，为每个快捷键创建 Action 并绑定 Lambda
  //    注意：这里**不检查槽位控件数量**。本函数可能在容器还未初始化（容量 0、
  //    槽位控件一个都没建）时被调用，那时按「槽位不足」跳过就等于整个快捷栏的
  //    快捷键全部失效且没人会再补一次。越界由 HandleHotkeyTriggered 在按下时兜底
  //    （那时容器要么已到位、要么这次按键本来就该被忽略）。
  for (int32 i = 0; i < HotkeyBindings.Num(); ++i) {
    if (i >= SlotWidgets.Num()) {
      WFLOG_INFO("UQuickBarWidget: 第 %d 个快捷键绑定时槽位控件尚未建出（容器容量还没"
                 "到位），先绑上，按键时按容器实际数据判断",
                 i);
    }
    const FHotkeyBinding &Binding = HotkeyBindings[i];
    if (!Binding.Chord.Key.IsValid() || Binding.FunctionName.IsNone())
      continue;

    // 4.1 动态创建 InputAction
    UInputAction *Action = NewObject<UInputAction>(
        this, *FString::Printf(TEXT("HotkeyAction_%d"), i));
    Action->ValueType = EInputActionValueType::Boolean;
    RuntimeActions.Add(Action);

    // 4.2 添加按键映射到上下文
    //     注意：此处仅映射主键；修饰键（Ctrl/Shift）需另行处理，见下文说明
    RuntimeContext->MapKey(Action, Binding.Chord.Key);

    // 4.3 用 Lambda 绑定，捕获 HotkeyIndex —— 这就是"预绑定的第一个参数"
    const int32 Index = i;

    EIC->BindActionValueLambda(Action, Binding.TriggerEvent,
                               [this, Index](const FInputActionValue &Value) {
                                 HandleHotkeyTriggered(Index, Value);
                               });
  }

  // 5) 把上下文加入子系统，使其生效
  Subsystem->AddMappingContext(RuntimeContext, MappingContextPriority);
  CachedSubsystem = Subsystem;
}

void UQuickBarWidget::HandleHotkeyTriggered(int32 HotkeyIndex,
                                            const FInputActionValue &Value) {
  if (!HotkeyBindings.IsValidIndex(HotkeyIndex))
    return;

  const FHotkeyBinding &Binding = HotkeyBindings[HotkeyIndex];
  APlayerController *PC = GetOwningPlayerController();
  if (!PC)
    return;

  // 校验修饰键是否匹配
  const bool bCtrlOK = !Binding.Chord.bCtrl ||
                       PC->IsInputKeyDown(EKeys::LeftControl) ||
                       PC->IsInputKeyDown(EKeys::RightControl);
  const bool bShiftOK = !Binding.Chord.bShift ||
                        PC->IsInputKeyDown(EKeys::LeftShift) ||
                        PC->IsInputKeyDown(EKeys::RightShift);
  const bool bAltOK = !Binding.Chord.bAlt ||
                      PC->IsInputKeyDown(EKeys::LeftAlt) ||
                      PC->IsInputKeyDown(EKeys::RightAlt);
  const bool bCmdOK = !Binding.Chord.bCmd ||
                      PC->IsInputKeyDown(EKeys::LeftCommand) ||
                      PC->IsInputKeyDown(EKeys::RightCommand);
  if (!(bCtrlOK && bShiftOK && bAltOK && bCmdOK))
    return;
  APawn *Owner = GetOwningPlayerPawn();
  if (!Owner)
    return;

  // 在 Owner 上按名称查找 UFUNCTION（蓝图自定义函数默认是 UFUNCTION）
  UFunction *Func = Owner->FindFunction(Binding.FunctionName);
  if (!Func) {
    WFLOG_WARNING("UQuickBarWidget: 函数 '%s' 未在 %s 上找到",
                  *Binding.FunctionName.ToString(), *Owner->GetName());
    return;
  }
  UItemContainer *LiveContainer = Container.Get();
  if (!LiveContainer)
    return;
  FItemInformation OutItem;
  if (!LiveContainer->GetItemAtSlot(HotkeyIndex, OutItem))
    return;
  // 槽位控件可能还没建出来（容器刚初始化 / 这一格超出当前容量）：
  // 上面取不到物品时已经返回了，这里再兜一层，别让越界下标把控件数组读穿。
  if (!SlotWidgets.IsValidIndex(HotkeyIndex)) {
    WFLOG_WARNING("UQuickBarWidget: 第 %d 个快捷键有物品但还没有对应的槽位控件"
                  "（容量 %d），本次忽略",
                  HotkeyIndex, SlotWidgets.Num());
    return;
  }
  FQuickSlotContext Context{OutItem, SlotWidgets[HotkeyIndex]};
  // 如果目标函数需要参数，可在此构造参数内存后再 ProcessEvent
  uint8 *ParamBuffer = (uint8 *)FMemory::Malloc(Func->ParmsSize);
  FMemory::Memzero(ParamBuffer, Func->ParmsSize);

  // 只处理一个参数，简单且安全
  for (TFieldIterator<FProperty> It(Func);
       It && It->HasAnyPropertyFlags(CPF_Parm); ++It) {
    if (FStructProperty *StructProp = CastField<FStructProperty>(*It)) {
      StructProp->CopyCompleteValue(
          StructProp->ContainerPtrToValuePtr<void>(ParamBuffer), &Context);
      break;
    }
  }
  Owner->ProcessEvent(Func, ParamBuffer);
  FMemory::Free(ParamBuffer);
}

APlayerController *UQuickBarWidget::GetOwningPlayerController() const {
  // 先取控件自己的 OwningPlayer：它在「Pawn 还没到位」的窗口里就是有效的
  // （客户端刚建控件、Pawn 尚未复制过来时，经 Pawn->GetController() 只会拿到空）。
  // 快捷键绑定的 InputComponent 长在控制器上，而控制器的 InputComponent 在
  // SetPlayer(ULocalPlayer*) 里就建好了（PlayerController.cpp:5256），比控件创建更早。
  if (APlayerController *OwningPC = GetOwningPlayer()) {
    return OwningPC;
  }

  if (APawn *Pawn = GetOwningPlayerPawn())
    return Cast<APlayerController>(Pawn->GetController());

  return nullptr;
}

void UQuickBarWidget::NativeDestruct() {
  // 清理映射上下文
  if (CachedSubsystem && RuntimeContext) {
    CachedSubsystem->RemoveMappingContext(RuntimeContext);
    CachedSubsystem = nullptr;
  }

  Super::NativeDestruct();
}