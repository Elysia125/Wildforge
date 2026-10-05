#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Styling/SlateTypes.h"
#include "InventoryUserWidget.generated.h"

class UButton;
class UTextBlock;
class UWidgetSwitcher;
class UItemContainerGrid;
class UItemContainer;
class UWidgetAnimation;
class UHorizontalBox;
class UBorder;

/** 主题配色与美化样式配置（可在蓝图子类的 Class Defaults 中直接微调） */
USTRUCT(BlueprintType)
struct FInventoryThemeConfig
{
	GENERATED_BODY()

	// --- 面板底色 ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Panel")
	FLinearColor MainPanelColor = FLinearColor(0.04f, 0.05f, 0.07f, 0.92f); // 暗黑科技/魔幻底色

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Panel")
	FLinearColor StatsPanelColor = FLinearColor(0.06f, 0.07f, 0.10f, 0.85f);

	// --- Tab 标签按钮样式 ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Tabs")
	FLinearColor TabActiveColor = FLinearColor(0.85f, 0.65f, 0.15f, 1.0f); // 选中时的高亮金/主色

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Tabs")
	FLinearColor TabInactiveColor = FLinearColor(0.12f, 0.13f, 0.16f, 0.8f); // 未选中暗色

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Tabs")
	FLinearColor TabHoverColor = FLinearColor(0.25f, 0.28f, 0.35f, 1.0f); // 悬停色

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Tabs")
	FLinearColor TextActiveColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Tabs")
	FLinearColor TextInactiveColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);

	// --- 整理按钮样式 ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Organize")
	FLinearColor OrganizeBtnNormal = FLinearColor(0.18f, 0.32f, 0.22f, 1.0f); // 典雅深绿

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Organize")
	FLinearColor OrganizeBtnHover = FLinearColor(0.25f, 0.45f, 0.30f, 1.0f);

	// --- 字体与文本美化 ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Text")
	FLinearColor PlayerNameColor = FLinearColor(1.0f, 0.82f, 0.35f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Text")
	FLinearColor TextShadowColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.75f); // 投影增强可读性

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Theme|Text")
	FVector2D TextShadowOffset = FVector2D(1.0f, 1.5f);
};

UCLASS()
class WILDFORGE_API UInventoryUserWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void InitializeInventory(UItemContainer* InContainer);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetPlayerName(const FText& InPlayerName);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ShowInventoryPage();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ShowCraftPage();

	UFUNCTION(BlueprintPure, Category = "Inventory")
	UItemContainerGrid* GetItemContainerGrid() const { return ItemContainerGrid; }

protected:
	virtual void NativeConstruct() override;

	// ========== 视觉美化核心方法 ==========
	/** 应用所有底色、阴影、通用按钮样式 */
	virtual void ApplyInitialThemeStyles();

	/** 根据当前选中的 Tab 刷新“背包/制作”高亮状态 */
	virtual void UpdateTabButtonVisuals();

	UFUNCTION()
	void OnInventoryButtonClicked();

	UFUNCTION()
	void OnCraftButtonClicked();

	UFUNCTION()
	void OnOrganizeButtonClicked();

	UFUNCTION(BlueprintNativeEvent, Category = "Inventory")
	void PlayButtonClickFeedback(UButton* ClickedButton);
	virtual void PlayButtonClickFeedback_Implementation(UButton* ClickedButton);

	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void OnInventoryPageShown();

	UFUNCTION(BlueprintImplementableEvent, Category = "Inventory")
	void OnCraftPageShown();

protected:
	/** 主题配置参数 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "UI Style")
	FInventoryThemeConfig ThemeConfig;

	// ========== 蓝图中已有的核心交互控件 ==========
	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UButton> InventoryButton;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UTextBlock> InventoryTextBlock;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UButton> CraftButton;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UTextBlock> CraftTextBlock;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UButton> OrganizeButton;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UTextBlock> OrganizeTextBlock;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UHorizontalBox> OrganizeHorizontalBox;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UWidgetSwitcher> PageSwitcher;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UItemContainerGrid> ItemContainerGrid;

	UPROPERTY(meta = (BindWidget), Transient)
	TObjectPtr<UTextBlock> PlayerNameText;

	// ========== 蓝图中已有的 Border（设为可选绑定，用于自动美化底板背景） ==========
	UPROPERTY(meta = (BindWidgetOptional), Transient)
	TObjectPtr<UBorder> InventoryCraftRootBorder;

	UPROPERTY(meta = (BindWidgetOptional), Transient)
	TObjectPtr<UBorder> PlayerRootBorder;

	UPROPERTY(meta = (BindWidgetOptional), Transient)
	TObjectPtr<UBorder> RightRootBorder;

	// ========== 蓝图动画 ==========
	UPROPERTY(meta = (BindWidgetAnim), Transient)
	TObjectPtr<UWidgetAnimation> CraftButtonClickAnim;

	UPROPERTY(meta = (BindWidgetAnim), Transient)
	TObjectPtr<UWidgetAnimation> InventoryButtonClickAnim;

	UPROPERTY(meta = (BindWidgetAnim), Transient)
	TObjectPtr<UWidgetAnimation> OrganizeButtonClickAnim;

private:
	UPROPERTY()
	TObjectPtr<UItemContainer> ItemContainer;

	int32 CurrentPageIndex = -1; // 默认初始化为 -1 确保首次切换生效

	static constexpr int32 PageIndex_Inventory = 0;
	static constexpr int32 PageIndex_Craft = 1;

	// 生成按钮样式的辅助函数
	FButtonStyle BuildButtonStyle(const FLinearColor& NormalColor, const FLinearColor& HoverColor, const FLinearColor& PressedColor);
};