// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/Components/QuickContainer.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

UQuickContainer::UQuickContainer() : UItemContainer() {
  setContainerType(EContainerType::PlayerHotbar);
  PrimaryComponentTick.bCanEverTick = false;
}

void UQuickContainer::SetupHotkeyBindings(
    UInputComponent *PlayerInputComponent) {
  // 1) 必须使用 Enhanced Input 组件
  UEnhancedInputComponent *EIC =
      Cast<UEnhancedInputComponent>(PlayerInputComponent);
  if (!EIC) {
    UE_LOG(
        LogTemp, Warning,
        TEXT(
            "UQuickContainer: 需要 Enhanced Input 系统（请检查项目输入设置）"));
    return;
  }

  // 2) 获取本地玩家子系统
  APlayerController *PC = GetOwningPlayerController();
  if (!PC || !PC->GetLocalPlayer()) {
    UE_LOG(LogTemp, Warning, TEXT("UQuickContainer: 无法获取 LocalPlayer"));
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
  for (int32 i = 0; i < HotkeyBindings.Num(); ++i) {
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

void UQuickContainer::HandleHotkeyTriggered(int32 HotkeyIndex,
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
  AActor *Owner = GetOwner();
  if (!Owner)
    return;

  // 在 Owner 上按名称查找 UFUNCTION（蓝图自定义函数默认是 UFUNCTION）
  UFunction *Func = Owner->FindFunction(Binding.FunctionName);
  if (!Func) {
    UE_LOG(LogTemp, Warning, TEXT("UQuickContainer: 函数 '%s' 未在 %s 上找到"),
           *Binding.FunctionName.ToString(), *Owner->GetName());
    return;
  }
  FItemInformation OutItem;
  if (!GetItemAtSlot(HotkeyIndex, OutItem))
    return;
  // 如果目标函数需要参数，可在此构造参数内存后再 ProcessEvent
  Owner->ProcessEvent(Func, &OutItem);
}

APlayerController *UQuickContainer::GetOwningPlayerController() const {
  if (APawn *Pawn = Cast<APawn>(GetOwner()))
    return Cast<APlayerController>(Pawn->GetController());

  return Cast<APlayerController>(GetOwner());
}

void UQuickContainer::EndPlay(const EEndPlayReason::Type EndPlayReason) {
  // 清理映射上下文
  if (CachedSubsystem && RuntimeContext) {
    CachedSubsystem->RemoveMappingContext(RuntimeContext);
    CachedSubsystem = nullptr;
  }

  Super::EndPlay(EndPlayReason);
}