#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"

#include "ToastStackWidget.generated.h"

class UCanvasPanel;
class UCanvasPanelSlot;
class UVerticalBox;

/**
 * 提示堆叠容器（**纯 C++ 构建，不需要 WBP**）。
 *
 * ## 布局
 *
 *   铺满视口的 CanvasPanel（SelfHitTestInvisible）
 *     └─ 顶部居中的 VerticalBox（AutoSize，距顶边 TopPadding 像素）
 *          ├─ 提示控件 0（最旧）
 *          ├─ 提示控件 1
 *          └─ ...
 *
 * `UToastStack` 把每条提示控件作为 `VerticalBox` 的子项加进来：
 *
 *   - **纵向排列由 UMG 负责**，所以任意多条都不可能重叠；
 *   - 移除某一项后，VerticalBox 自动重排，下面的提示整体上移一格；
 *   - 每条提示的计时器在各自控件里，重排**不会影响剩余时间**；
 *   - 每行的间距由 `UToastStack` 写进 VerticalBoxSlot 的 padding（`RowGap`）。
 *
 * 卡片高度不需要在这里测量：VerticalBox 直接采用每个提示控件的期望尺寸
 *（提示 WBP 的根 CanvasPanel 按 BackgroundBorder 的固定布局算出 800×60；
 * 以后把卡片改成 SizeToContent 也会自动适配逐条高度）。
 *
 * 纯客户端表现对象：不复制、没有 RPC、不接管输入模式。
 */
UCLASS()
class WILDFORGE_API UToastStackWidget : public UUserWidget {
  GENERATED_BODY()

public:
  /**
   * 设置容器距屏幕顶边的距离（像素），立即生效。
   *
   * 由 `UToastStack` 在创建时用提示控件类默认值上的 `TopPadding` 调用。
   */
  void ConfigureLayout(float InTopPadding);

  /**
   * 放提示控件的纵向容器。
   *
   * 首次访问时若控件树还没建好会补建一次（正常由 `NativeOnInitialized` 建好）；
   * 建不出来时返回 nullptr，调用方负责记错误日志并放弃本次提示。
   */
  UVerticalBox *GetToastContainer();

protected:
  // UUserWidget
  virtual void NativeOnInitialized() override;
  virtual TSharedRef<SWidget> RebuildWidget() override;

private:
  /**
   * 构建控件树（幂等）。
   *
   * 原生 `UUserWidget` 没有蓝图 WidgetTree 模板，`Initialize()` 会给一个空树；
   * 这里手动建根 CanvasPanel 与 VerticalBox，并把 VerticalBox 锚到顶部居中。
   */
  void BuildWidgetTree();

  /** 根画布：铺满视口，只负责给 VerticalBox 提供顶部居中的锚点区域。 */
  UPROPERTY(Transient)
  TObjectPtr<UCanvasPanel> RootCanvas;

  /** 提示控件的纵向容器（由 `UToastStack` 往里加 / 删控件）。 */
  UPROPERTY(Transient)
  TObjectPtr<UVerticalBox> ToastContainer;

  /** VerticalBox 在根画布上的槽（用于改顶部间距）。 */
  UPROPERTY(Transient)
  TObjectPtr<UCanvasPanelSlot> ToastContainerSlot;

  /** 容器距屏幕顶边的距离（像素）。 */
  float TopPadding = 40.0f;

  /** 是否已经尝试过构建控件树（避免失败后每次访问都重建 / 刷日志）。 */
  bool bTriedBuildWidgetTree = false;
};
