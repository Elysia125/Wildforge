#include "ItemSystem/UI/InventoryUserWidget.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Components/WidgetSwitcher.h"
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/UI/ItemContainerGrid.h"

void UInventoryUserWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 1. 应用面板、文字阴影、按钮等静态美化样式
	ApplyInitialThemeStyles();

	// 2. 绑定按钮点击事件
	if (InventoryButton)
	{
		InventoryButton->OnClicked.AddDynamic(this, &UInventoryUserWidget::OnInventoryButtonClicked);
	}

	if (CraftButton)
	{
		CraftButton->OnClicked.AddDynamic(this, &UInventoryUserWidget::OnCraftButtonClicked);
	}

	if (OrganizeButton)
	{
		OrganizeButton->OnClicked.AddDynamic(this, &UInventoryUserWidget::OnOrganizeButtonClicked);
	}

	// 3. 查找容器初始化
	if (APawn* Pawn = GetOwningPlayerPawn())
	{
		if (UItemContainer* FoundContainer = Pawn->FindComponentByClass<UItemContainer>())
		{
			InitializeInventory(FoundContainer);
		}
	}

	// 4. 默认显示并高亮背包页
	ShowInventoryPage();
}

FButtonStyle UInventoryUserWidget::BuildButtonStyle(const FLinearColor& NormalColor, const FLinearColor& HoverColor, const FLinearColor& PressedColor)
{
	FButtonStyle Style;
	Style.Normal.TintColor = FSlateColor(NormalColor);
	Style.Hovered.TintColor = FSlateColor(HoverColor);
	Style.Pressed.TintColor = FSlateColor(PressedColor);
	
	// 设置优雅的内边距，提升按钮点击区域舒适感
	Style.NormalPadding = FMargin(16.f, 6.f);
	Style.PressedPadding = FMargin(16.f, 8.f, 16.f, 4.f); // 按下时有微沉感
	return Style;
}

void UInventoryUserWidget::ApplyInitialThemeStyles()
{
	// --- 1. 美化各级面板背景 (Border) ---
	if (InventoryCraftRootBorder)
	{
		InventoryCraftRootBorder->SetBrushColor(ThemeConfig.MainPanelColor);
	}
	if (PlayerRootBorder)
	{
		PlayerRootBorder->SetBrushColor(ThemeConfig.StatsPanelColor);
	}
	if (RightRootBorder)
	{
		RightRootBorder->SetBrushColor(ThemeConfig.StatsPanelColor);
	}

	// --- 2. 美化文本：添加投影、增强立体感和可读性 ---
	auto BeautifyText = [this](UTextBlock* TextBlock, const FLinearColor& TextColor)
	{
		if (!TextBlock) return;
		TextBlock->SetColorAndOpacity(FSlateColor(TextColor));
		TextBlock->SetShadowOffset(ThemeConfig.TextShadowOffset);
		TextBlock->SetShadowColorAndOpacity(ThemeConfig.TextShadowColor);
	};

	BeautifyText(PlayerNameText, ThemeConfig.PlayerNameColor);
	BeautifyText(OrganizeTextBlock, FLinearColor::White);

	// --- 3. 美化整理按钮 ---
	if (OrganizeButton)
	{
		FButtonStyle OrganizeStyle = BuildButtonStyle(
			ThemeConfig.OrganizeBtnNormal,
			ThemeConfig.OrganizeBtnHover,
			ThemeConfig.OrganizeBtnNormal * 0.7f
		);
		OrganizeButton->SetStyle(OrganizeStyle);
	}
}

void UInventoryUserWidget::UpdateTabButtonVisuals()
{
	const bool bIsInventory = (CurrentPageIndex == PageIndex_Inventory);

	// 选中态 vs 未选中态颜色
	const FLinearColor InvBtnColor = bIsInventory ? ThemeConfig.TabActiveColor : ThemeConfig.TabInactiveColor;
	const FLinearColor CraftBtnColor = !bIsInventory ? ThemeConfig.TabActiveColor : ThemeConfig.TabInactiveColor;

	const FLinearColor InvTextColor = bIsInventory ? ThemeConfig.TextActiveColor : ThemeConfig.TextInactiveColor;
	const FLinearColor CraftTextColor = !bIsInventory ? ThemeConfig.TextActiveColor : ThemeConfig.TextInactiveColor;

	// 背包 Tab 按钮态
	if (InventoryButton)
	{
		FButtonStyle InvStyle = BuildButtonStyle(InvBtnColor, ThemeConfig.TabHoverColor, ThemeConfig.TabActiveColor);
		InventoryButton->SetStyle(InvStyle);
	}
	if (InventoryTextBlock)
	{
		InventoryTextBlock->SetColorAndOpacity(FSlateColor(InvTextColor));
		InventoryTextBlock->SetShadowOffset(ThemeConfig.TextShadowOffset);
		InventoryTextBlock->SetShadowColorAndOpacity(ThemeConfig.TextShadowColor);
	}

	// 制作 Tab 按钮态
	if (CraftButton)
	{
		FButtonStyle CraftStyle = BuildButtonStyle(CraftBtnColor, ThemeConfig.TabHoverColor, ThemeConfig.TabActiveColor);
		CraftButton->SetStyle(CraftStyle);
	}
	if (CraftTextBlock)
	{
		CraftTextBlock->SetColorAndOpacity(FSlateColor(CraftTextColor));
		CraftTextBlock->SetShadowOffset(ThemeConfig.TextShadowOffset);
		CraftTextBlock->SetShadowColorAndOpacity(ThemeConfig.TextShadowColor);
	}
}

void UInventoryUserWidget::InitializeInventory(UItemContainer* InContainer)
{
	ItemContainer = InContainer;

	if (ItemContainerGrid && ItemContainer)
	{
		ItemContainerGrid->InitializeGrid(ItemContainer, 5);
	}
}

void UInventoryUserWidget::SetPlayerName(const FText& InPlayerName)
{
	if (PlayerNameText)
	{
		PlayerNameText->SetText(InPlayerName);
	}
}

void UInventoryUserWidget::ShowInventoryPage()
{
	if (CurrentPageIndex == PageIndex_Inventory)
	{
		return;
	}

	CurrentPageIndex = PageIndex_Inventory;

	if (PageSwitcher)
	{
		PageSwitcher->SetActiveWidgetIndex(PageIndex_Inventory);
	}
	if (OrganizeHorizontalBox)
	{
		OrganizeHorizontalBox->SetVisibility(ESlateVisibility::Visible);
	}

	// 刷新标签选中高亮视觉
	UpdateTabButtonVisuals();

	OnInventoryPageShown();
}

void UInventoryUserWidget::ShowCraftPage()
{
	if (CurrentPageIndex == PageIndex_Craft)
	{
		return;
	}

	CurrentPageIndex = PageIndex_Craft;

	if (PageSwitcher)
	{
		PageSwitcher->SetActiveWidgetIndex(PageIndex_Craft);
	}
	if (OrganizeHorizontalBox)
	{
		OrganizeHorizontalBox->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 刷新标签选中高亮视觉
	UpdateTabButtonVisuals();

	OnCraftPageShown();
}

void UInventoryUserWidget::OnInventoryButtonClicked()
{
	ShowInventoryPage();
	PlayButtonClickFeedback(InventoryButton);
}

void UInventoryUserWidget::OnCraftButtonClicked()
{
	ShowCraftPage();
	PlayButtonClickFeedback(CraftButton);
}

void UInventoryUserWidget::OnOrganizeButtonClicked()
{
	if (ItemContainer)
	{
		// 必须走 Server RPC：OrganizeContainer 是 BlueprintAuthorityOnly，
		// UI 运行在客户端，直接调用会被静默丢弃（只有单机/主机才看起来有效）
		ItemContainer->Server_OrganizeContainer();
	}
	PlayButtonClickFeedback(OrganizeButton);
}

void UInventoryUserWidget::PlayButtonClickFeedback_Implementation(UButton* ClickedButton)
{
	if (ClickedButton == InventoryButton && InventoryButtonClickAnim)
	{
		PlayAnimation(InventoryButtonClickAnim);
	}
	else if (ClickedButton == CraftButton && CraftButtonClickAnim)
	{
		PlayAnimation(CraftButtonClickAnim);
	}
	else if (ClickedButton == OrganizeButton && OrganizeButtonClickAnim)
	{
		PlayAnimation(OrganizeButtonClickAnim);
	}
}