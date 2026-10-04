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
    /** 初始化网格：关联容器、设置每行个数，并立即刷新 */
    UFUNCTION(BlueprintCallable, Category = "ItemContainerGrid")
    void InitializeGrid(UItemContainer* InContainer, int32 InSlotsPerRow = 5);

    /** 根据容器数据重新生成/刷新所有槽位 */
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

    /** 容器内容变化回调，触发整表刷新 */
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

    /** 更新单个槽位的显示 */
    void UpdateSlot(int32 SlotIndex, UInventorySlotWidget* SlotWidget);

    /** 保证槽位 Widget 数量恒等于容量：不足补建、多余移除（空槽也保留展示） */
    void EnsureSlotCount(int32 DesiredCount);

    /** 按 SlotsPerRow 重新排布所有已存在的槽位 */
    void LayoutSlots();
};