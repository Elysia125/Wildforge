// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Structs/HotkeyBinding.h"
#include "ItemSystem/enums/ContainerType.h"

#include "QuickContainer.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;
/**
 *
 */
UCLASS()
class WILDFORGE_API UQuickContainer : public UItemContainer {
  GENERATED_BODY()
public:
    UQuickContainer();
protected:
  // 蓝图可编辑的快捷键数组
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotkey")
  TArray<FHotkeyBinding> HotkeyBindings;
  // 映射上下文优先级
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotkey")
  int32 MappingContextPriority = 0;
  // 供蓝图调用的动态绑定函数
  UFUNCTION(BlueprintCallable, Category = "Hotkey")
  void SetupHotkeyBindings(UInputComponent *PlayerInputComponent);
  virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:

  // 运行时动态创建的 InputAction（每个快捷键一个）
  UPROPERTY(Transient)
  TArray<TObjectPtr<UInputAction>> RuntimeActions;

  // 运行时动态创建的映射上下文
  UPROPERTY(Transient)
  TObjectPtr<UInputMappingContext> RuntimeContext;

  // 缓存子系统，便于 EndPlay 时移除上下文
  UPROPERTY(Transient)
  TObjectPtr<UEnhancedInputLocalPlayerSubsystem> CachedSubsystem;

  // 统一处理函数：第一个参数 HotkeyIndex 就是被预绑定的那个参数
  void HandleHotkeyTriggered(int32 HotkeyIndex, const FInputActionValue &Value);

  APlayerController *GetOwningPlayerController() const;
};
