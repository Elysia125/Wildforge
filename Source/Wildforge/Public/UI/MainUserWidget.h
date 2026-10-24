// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Blueprint/UserWidget.h"
#include "ItemSystem/UI/InventoryUserWidget.h"
#include "ItemSystem/UI/QuickBarWidget.h"

#include "MainUserWidget.generated.h"

class UCharacterAttributes;
class UImage;
class UProgressBar;
class UTextBlock;
class UWidget;

/**
 *
 */
UCLASS()
class WILDFORGE_API UMainUserWidget : public UUserWidget {
  GENERATED_BODY()
protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;

  /** 占有关系变化（含客户端「Pawn 复制到位后被占有」）时重试初始化快捷栏
   *  与属性绑定。客户端上本地 PlayerController 的 BeginPlay 往往早于 Pawn
   *  到位，那一刻 GetOwningPlayerPawn() 还是空，不补这一枪就整块不工作。 */
  UFUNCTION()
  void HandlePossessedPawnChanged(APawn *OldPawn, APawn *NewPawn);

  /** 找 Pawn 上的 UQuickContainer 并交给 QuickBarWidget。
   *  容器容量此刻**可能还是 0**（见 .cpp 里的时序说明），这是正常的：
   *  容量由容器自己广播 OnContainerChanged 时补课，这里不拿它做条件判断。 */
  void TryInitializeQuickBar();

  /** 找 Pawn 上的 UCharacterAttributes 并订阅生命/精力/等级/经验事件。
   *  客户端上属性可能还没复制到位，本函数会先按当前值刷新一次，
   *  后续由 OnRep 委托持续更新。 */
  void TryInitializeAttributes();

  /** 退订旧属性组件的所有委托（Pawn 变化 / 控件销毁时调用）。 */
  void UnbindAttributes();

  /** 把属性组件的当前值刷到血条、精力条、经验条与等级文本上。 */
  void RefreshAttributeWidgets();

  /** 按生命/精力上限把两条 bar 拉长：长度 = 设计宽度 × (当前上限 / 1 级上限)，
   *  倍率夹在 [1, MaxBarStretch]。只有客户端表现，不参与复制。 */
  void UpdateAttributeBarLengths();

  /** 缓存设计器里的 bar 槽位尺寸（之后会被拉伸改写，必须在第一次刷新前抓下来）。
   *  控件被重复 Construct 时不会覆盖已缓存的基准值。 */
  void CacheAttributeBarBaseSizes();

  /** 修改控件 CanvasPanelSlot 的尺寸；槽位不是 CanvasPanelSlot 时静默跳过
   *  （缓存阶段已经报过警告，避免每次刷新刷屏）。 */
  void ResizeAttributeBar(UWidget *Bar, float Width, float Height);

  UFUNCTION()
  void HandleHealthChanged(float NewHealth);

  UFUNCTION()
  void HandleMaxHealthChanged(float NewMaxHealth);

  UFUNCTION()
  void HandleEnergyChanged(float NewEnergy);

  UFUNCTION()
  void HandleMaxEnergyChanged(float NewMaxEnergy);

  UFUNCTION()
  void HandleLevelChanged(int32 NewLevel);

  UFUNCTION()
  void HandleExperienceChanged(int32 NewExperience,
                               int32 NewExperienceToNextLevel);

public:

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UInventoryUserWidget> InventoryWidget;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Inventory")
  TObjectPtr<UQuickBarWidget> QuickBarWidget;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
  bool bIsInventoryOpen = false;

  // ===== 主界面属性 HUD（UMG 里的控件名必须与属性名完全一致）=====

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Attributes")
  TObjectPtr<UProgressBar> HPProgressBar;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Attributes")
  TObjectPtr<UProgressBar> EnergyProgressBar;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidget), Category = "Attributes")
  TObjectPtr<UProgressBar> ExperienceProgressBar;

  /** 等级文本。资产里当前叫 TextBlock_53，重命名为 LevelText 后即可自动绑定；
   *  没绑定时只跳过文本更新，不影响三条进度条。 */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Attributes")
  TObjectPtr<UTextBlock> LevelText;

  /** 头像与地图目前只由 WBP 的静态资源驱动；C++ 预留绑定，方便以后换成
   *  动态纹理（SetBrushFromTexture）。 */
  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Attributes")
  TObjectPtr<UImage> AvatarImage;

  UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional), Category = "Attributes")
  TObjectPtr<UImage> MapImage;

  // ===== 属性条随上限拉伸（纯客户端表现，写在 WBP 类默认值里即可）=====

  /** 血条/精力条是否随上限增长而变长。关闭时恢复设计器里的原始宽度。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attributes|Bar Stretch")
  bool bStretchBarsWithMaxValue = true;

  /** 最大拉伸倍率（相对设计宽度）。默认 2.5：生命上限 1 级 100 → 满级前条子
   *  最多 250×2.5=625 宽，1:1 布局下也不会顶到右上角地图。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite,
            Category = "Attributes|Bar Stretch",
            meta = (EditCondition = "bStretchBarsWithMaxValue", ClampMin = "1.0"))
  float MaxBarStretch = 2.5f;

  UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
  void ShowInventory();

  // 默认实现，函数名必须加 _Implementation
  virtual void ShowInventory_Implementation();

private:
  // 弱引用：Pawn/组件被销毁或替换后不会留下悬空指针（与背包控件的约定一致）。
  UPROPERTY(Transient)
  TWeakObjectPtr<UCharacterAttributes> BoundAttributes;

  /** 设计器里的 bar 原始尺寸（NativeConstruct 时抓一次）。
   *  不能用「当前槽位尺寸」当基准——它已经被上一次拉伸改写过。 */
  FVector2D BaseHPBarSize = FVector2D::ZeroVector;
  FVector2D BaseEnergyBarSize = FVector2D::ZeroVector;

  /** 上一次实际应用的拉伸倍率，仅用于「变化时才记日志」，避免掉血每帧刷日志。 */
  float LastAppliedHPBarStretch = -1.0f;
  float LastAppliedEnergyBarStretch = -1.0f;
};
