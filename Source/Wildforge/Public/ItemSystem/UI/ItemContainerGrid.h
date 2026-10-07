#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemContainerGrid.generated.h"

class UScrollBox;
class UUniformGridPanel;
class UItemContainer;
class UInventorySlotWidget;

UCLASS()
class WILDFORGE_API UItemContainerGrid : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 初始化网格：关联容器、设置每行个数，并立即刷新。
     *  InSlotsPerRow <= 0（默认）表示沿用控件上配置的 SlotsPerRow —— 也就是 WBP 设计器里
     *  设的每行个数；调用方不需要、也不应该再硬编码列数。 */
    UFUNCTION(BlueprintCallable, Category = "ItemContainerGrid")
    void InitializeGrid(UItemContainer* InContainer, int32 InSlotsPerRow = 0);

    /** 根据容器数据重建：保证槽位数量与布局，并让每个槽位重新绑定来源 + 拉一次数据。
     *  容器内容变化时会自动走 HandleContainerChanged（结构）+ 各槽位自己的订阅（内容），
     *  这个全量入口留给「初始化 / 换容器 / 改每行个数」。 */
    UFUNCTION(BlueprintCallable, Category = "ItemContainerGrid")
    void RefreshGrid();

    /** 获取当前关联的容器 */
    UFUNCTION(BlueprintPure, Category = "ItemContainerGrid")
    UItemContainer* GetContainer() const { return Container; }

    /** 获取每行个数 */
    UFUNCTION(BlueprintPure, Category = "ItemContainerGrid")
    int32 GetSlotsPerRow() const { return SlotsPerRow; }

    /** 设置每行个数，并刷新 */
    UFUNCTION(BlueprintCallable, Category = "ItemContainerGrid")
    void SetSlotsPerRow(int32 InSlotsPerRow);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

    /** 容器内容变化回调：只处理结构（容量变化 -> 增删槽位控件）。
     *  内容不在这里刷——每个槽位都订阅了同一个委托，会自己按 (容器, 索引) 拉数据 */
    UFUNCTION()
    void HandleContainerChanged();

    /** 订阅当前容器的变更委托（会先解绑，避免重复绑定） */
    void BindToContainer();

    /** 解绑当前容器的变更委托 */
    void UnbindFromContainer();

    /** 绑定的滚动框 */
    UPROPERTY(meta = (BindWidget), Transient)
    TObjectPtr<UScrollBox> ScrollBoxRoot;

    /** 绑定的均匀网格面板 */
    UPROPERTY(meta = (BindWidget), Transient)
    TObjectPtr<UUniformGridPanel> MainUniformGridPanel;

    /** 槽位 Widget 类，应设置为 W_InventorySlot 或继承自 UInventorySlotWidget 的蓝图 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemContainerGrid")
    TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

    /** 每行槽位个数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemContainerGrid")
    int32 SlotsPerRow = 5;

    /** 当前关联的容器 */
    UPROPERTY(BlueprintReadOnly, Category = "ItemContainerGrid")
    TObjectPtr<UItemContainer> Container;

    /** 已生成的槽位 Widget 列表 */
    UPROPERTY()
    TArray<TObjectPtr<UInventorySlotWidget>> SlotWidgets;

    /** 保证槽位 Widget 数量恒等于容量：不足补建、多余移除（空槽也保留展示）。
     *  返回值：数量是否变化过（调用方据此决定要不要重排布局）。 */
    bool EnsureSlotCount(int32 DesiredCount);

    /** 按 SlotsPerRow 重新排布所有已存在的槽位 */
    void LayoutSlots();
};