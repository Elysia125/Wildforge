#pragma once

#include "CoreMinimal.h"

#include "ItemSystem/Structs/HotkeyBinding.h"
#include "ItemSystem/UI/ItemContainerGrid.h"

#include "QuickBarWidget.generated.h"

class UInputAction;
class UInputMappingContext;
class UEnhancedInputLocalPlayerSubsystem;
struct FInputActionValue;

UCLASS()
class WILDFORGE_API UQuickBarWidget : public UItemContainerGrid {
  GENERATED_BODY()
public:
  virtual void InitializeGrid(UItemContainer *InContainer,
                              int32 InSlotsPerRow = 0) override;

protected:
  /** 容器结构变化（容量到位 / 增删槽位）后的收尾：先把「每行个数」校正为容量
   *  （快捷栏 = 单行），再交给基类增删槽位，最后补按键标签。
   *  ⚠️ 重写父类的 UFUNCTION **不能**再写 UFUNCTION() 声明 —— UHT 直接报错
   *  "cannot have a UFUNCTION() declaration above it"（参数沿用父类声明）。
   *  动态委托按函数名在实例的实际类上查找，所以这里重写仍然会被调到。 */
  virtual void HandleContainerChanged() override;

  /** 按 HotkeyBindings 给每个已存在的槽位写上按键标签（超出绑定的写 <None>）。
   *  槽位控件是容器广播时**补建**的，所以这个函数会被反复调用：
   *  只在文本真的变了才写，避免每次容器广播都刷一遍 Slate 与日志。 */
  void RefreshHotkeyLabels();

  // 蓝图可编辑的快捷键数组
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuickBar")
  TArray<FHotkeyBinding> HotkeyBindings;
  // 映射上下文优先级
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "QuickBar")
  int32 MappingContextPriority = 0;
  // 供蓝图调用的动态绑定函数
  UFUNCTION(BlueprintCallable, Category = "QuickBar")
  void SetupHotkeyBindings(UInputComponent *PlayerInputComponent);

  virtual void NativeDestruct() override;

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