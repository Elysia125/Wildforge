#pragma once

#include "CoreMinimal.h"

#include "Blueprint/UserWidget.h"
#include "UI/ToastMessage.h"

#include "ToastWidget.generated.h"

class UBorder;
class UImage;
class UTextBlock;
class UToastWidget;

/**
 * 一条提示播完（淡出结束、控件回到空闲）时广播。
 *
 * Sender = 刚播完的控件；Toast = 它刚显示的内容。仅 C++ 使用：
 * 多提示堆叠器 `UToastStack` 据此回收控件、把下面的提示整体上移并从队列补位。
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FWildforgeToastFinished, UToastWidget *,
                                     const FToastMessage &);

/**
 * Toast 提示控件基类（必须由蓝图子类提供具体外观）。
 *
 * ## 功能
 *
 * 一个控件实例负责**一条**提示的显示与淡入淡出计时；内部队列仍保留（同时到达的
 * 其余提示排队，当前提示淡出后自动接着播），方便蓝图单独使用这个控件。
 *
 * 管理器（`UWildforgeUIManager`）走的是「多条同时显示」的路径：
 * `UToastStack` 为每条提示创建一个独立实例，加进 `UToastStackWidget` 的
 * `VerticalBox` 里纵向排列（UMG 负责布局，天然不重叠；播完移除后其余自动上移）。
 * 每个实例只显示一条提示，所以**不会复用**已经播完的控件（复用会把 WBP 淡出
 * 动画写过的子控件不透明度带进下一条，见 bug-149 / bug-159）。
 *
 * ## 必需 / 可选绑定（UMG 里的控件名必须逐字一致）
 *
 *   - `MessageText`（**必需**，UTextBlock）——提示文本；
 *   - `BackgroundBorder`（可选，UBorder）——背景色；
 *   - `IconImage`（可选，UImage）——图标。
 *
 * ## 淡入淡出
 *
 * 默认由 C++ 在 `NativeTick` 里驱动整个控件的 `RenderOpacity`：
 *
 *   淡入 FadeInDuration -> 停留 Duration -> 淡出 FadeOutDuration -> 下一条
 *
 * 想让 WBP 用自定义 Widget 动画（位移 / 缩放 / 曲线）接管时：
 *
 *   1. 在类默认值里把 `bUseNativeFade` 关掉（C++ 不再写 RenderOpacity）；
 *   2. 在 `BP_OnToastShown` 里播放淡入动画；
 *   3. 在 `BP_OnToastFadeOutStarted` 里播放淡出动画；
 *   4. 把 `FadeInDuration` / `FadeOutDuration` 调成与动画时长一致——
 *      C++ 仍按这两个值推进队列，动画只是表现。
 *
 * ## 生命周期
 *
 * Slate 控件会被释放并重建（`NativeConstruct` 可能重复执行），所以文本、颜色、
 * 当前不透明度都保存在控件状态里，`NativeConstruct` 时按状态重放，不依赖
 * 「先构造再灌数据」的顺序。控件还没进入 Slate 树时收到的提示会先入队，
 * 等 `NativeConstruct` 之后再开始播放，避免蓝图动画在控件树就绪前被触发。
 */
UCLASS(Abstract)
class WILDFORGE_API UToastWidget : public UUserWidget {
  GENERATED_BODY()

private:
  /** 内部播放阶段（不参与复制 / 反射）。 */
  enum class EToastPhase : uint8 {
    Idle,
    FadeIn,
    Hold,
    FadeOut,
  };

public:
  /**
   * 用控件上配置的默认样式弹出一条提示。
   *
   * 停留时长使用 `DefaultDuration`；颜色 / 图标使用 `Toast|Defaults` 下的默认值。
   */
  UFUNCTION(BlueprintCallable, Category = "Toast")
  void ShowToast(const FText &Message);

  /**
   * 用指定文本颜色、背景颜色与停留时长弹出一条提示。
   *
   * Duration <= 0 时回退到 `DefaultDuration`。图标仍使用默认图标。
   */
  UFUNCTION(BlueprintCallable, Category = "Toast")
  void ShowToastStyled(const FText &Message, FLinearColor TextColor,
                       FLinearColor BackgroundColor, float Duration);

  /**
   * 完整数据入口：文本、颜色、图标、停留时长都可逐条指定。
   *
   * 若当前已有提示正在显示，这条会进入待显示队列；队列满时（`MaxQueuedMessages`）
   * 丢弃**排队最久**的一条并记 WARNING。
   */
  UFUNCTION(BlueprintCallable, Category = "Toast")
  void ShowToastMessage(const FToastMessage &Message);

  /**
   * 清空待显示队列。
   *
   * @param bClearActive true = 当前正在显示的提示也立即收起；false = 让它自然播完。
   */
  UFUNCTION(BlueprintCallable, Category = "Toast")
  void ClearToasts(bool bClearActive = true);

  /** 当前是否有提示正在显示（含淡入 / 停留 / 淡出阶段）。 */
  UFUNCTION(BlueprintPure, Category = "Toast")
  bool IsToastActive() const { return Phase != EToastPhase::Idle; }

  /** 待显示队列中的提示数量（不含当前正在显示的那条）。 */
  UFUNCTION(BlueprintPure, Category = "Toast")
  int32 GetPendingToastCount() const { return PendingToasts.Num(); }

  /** 当前正在显示的提示（空闲时返回默认结构体）。 */
  UFUNCTION(BlueprintPure, Category = "Toast")
  FToastMessage GetActiveToast() const { return ActiveToast; }

  /** 当前是否处于淡出阶段（淡出中的提示不再参与「相同文本合并」）。 */
  UFUNCTION(BlueprintPure, Category = "Toast")
  bool IsToastFadingOut() const { return Phase == EToastPhase::FadeOut; }

  /** 同时最多显示几条提示（`UToastStack` 读取；<= 0 按 1 处理）。 */
  UFUNCTION(BlueprintPure, Category = "Toast|Stack")
  int32 GetMaxVisibleToasts() const { return MaxVisibleToasts; }

  /** 多条提示上下堆叠时相邻两行卡片的间距（像素，`UToastStack` 读取）。 */
  UFUNCTION(BlueprintPure, Category = "Toast|Stack")
  float GetRowGap() const { return RowGap; }

  /** 堆叠容器距屏幕顶边的距离（像素，`UToastStack` 读取）。 */
  UFUNCTION(BlueprintPure, Category = "Toast|Stack")
  float GetTopPadding() const { return TopPadding; }

  /** 待显示队列上限（<= 0 表示不限制，`UToastStack` 读取）。 */
  UFUNCTION(BlueprintPure, Category = "Toast|Queue")
  int32 GetMaxQueuedMessages() const { return MaxQueuedMessages; }

  /** 相同文本的提示是否合并（`UToastStack` 读取；默认 false）。 */
  UFUNCTION(BlueprintPure, Category = "Toast|Queue")
  bool GetMergeSameText() const { return bMergeSameText; }

  /**
   * 一条提示播完（控件回到空闲）时广播。仅 C++：多提示堆叠器据此回收控件。
   *
   * 控件内部队列接着播下一条时**不会**广播（那时控件不空闲）。
   */
  FWildforgeToastFinished OnToastFinished;

  /**
   * 用某个提示控件类（WBP 子类）的**类默认值**补全一条「默认样式」提示。
   *
   * 管理器在创建控件实例之前就需要完整的 `FToastMessage`（排队 / 合并都靠它），
   * 所以在类上提供入口，避免管理器直接读控件的属性。
   */
  static FToastMessage MakeDefaultToastMessage(const UClass *ToastClass,
                                               const FText &Message);

  /** 同上，但指定文本颜色 / 背景颜色 / 停留时长（图标仍取类默认值）。 */
  static FToastMessage MakeStyledToastMessage(const UClass *ToastClass,
                                              const FText &Message,
                                              FLinearColor TextColor,
                                              FLinearColor BackgroundColor,
                                              float Duration);

protected:
  virtual void NativeConstruct() override;
  virtual void NativeDestruct() override;
  virtual void NativeTick(const FGeometry &MyGeometry, float InDeltaTime) override;

  // ================= 蓝图表现钩子 =================

  /** 一条提示开始显示（已写入文本 / 颜色，淡入开始前）。在这里播放淡入动画。 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Toast")
  void BP_OnToastShown(const FToastMessage &Toast);

  /** 一条提示进入淡出阶段。在这里播放淡出动画。 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Toast")
  void BP_OnToastFadeOutStarted(const FToastMessage &Toast);

  /** 一条提示已经收起（下一条即将开始，或队列已空）。 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Toast")
  void BP_OnToastHidden(const FToastMessage &Toast);

  /** 队列已清空、当前没有提示在显示。 */
  UFUNCTION(BlueprintImplementableEvent, Category = "Toast")
  void BP_OnToastQueueEmptied();

  // ================= 默认样式 =================

  /** ShowToast 使用的文本颜色。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Defaults")
  FLinearColor DefaultTextColor = FLinearColor::White;

  /** ShowToast 使用的背景颜色（WBP 绑定了 BackgroundBorder 时才生效）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Defaults")
  FLinearColor DefaultBackgroundColor =
      FLinearColor(0.0f, 0.0f, 0.0f, 0.75f);

  /** ShowToast 使用的默认图标（可为空）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Defaults")
  TObjectPtr<UTexture2D> DefaultIcon = nullptr;

  /** ShowToast 使用的默认图标着色。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Defaults")
  FLinearColor DefaultIconTint = FLinearColor::White;

  // ================= 时长与队列 =================

  /**
   * 一条提示完全显示（不透明度 = 1）的默认停留时长，单位秒。
   *
   * 总时长 = FadeInDuration + 停留时长 + FadeOutDuration。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Timing",
            meta = (ClampMin = "0.0", UIMin = "0.0"))
  float DefaultDuration = 4.0f;

  /** 淡入时长（秒）。<= 0 表示立即完全显示。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Timing",
            meta = (ClampMin = "0.0", UIMin = "0.0"))
  float FadeInDuration = 0.2f;

  /** 淡出时长（秒）。<= 0 表示直接收起。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Timing",
            meta = (ClampMin = "0.0", UIMin = "0.0"))
  float FadeOutDuration = 0.25f;

  /**
   * 待显示队列上限（不含正在显示的那几条）。
   *
   * <= 0 表示不限制。队列已满时新提示仍会入队，但会丢弃排队最久的一条。
   * 管理器路径下由 `UToastStack` 读取（取提示控件 WBP 子类的类默认值）。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Queue",
            meta = (ClampMin = "0", UIMin = "0"))
  int32 MaxQueuedMessages = 5;

  /**
   * 相同文本的提示是否合并（`UToastStack` 读取）。
   *
   * 默认 **false**：每次触发都新开一格，重复触发同一条警告（例如连续按冲刺）
   * 会占满多个格子，便于确认「同时显示 N 条」的效果。
   *
   * 打开后回到「相同文本只保留一条」：正在显示的只重置停留计时，队列尾部的
   * 相同文本直接替换。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Queue")
  bool bMergeSameText = false;

  /**
   * 同时最多显示几条提示（`UToastStack` 读取）。
   *
   * 超过这个数量后新提示进入待显示队列；某一格播完时，它下面的提示整体上移一格，
   * 队列头部补进空出来的最后一格——**每条提示的剩余时间不受移动影响**。
   * <= 0 按 1 处理。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Stack",
            meta = (ClampMin = "1", UIMin = "1"))
  int32 MaxVisibleToasts = 5;

  /**
   * 多条提示上下堆叠时，相邻两行卡片之间的间距（像素）。
   *
   * 由 `UToastStackWidget` 的 `VerticalBox` 用行尾 padding 实现；卡片高度由
   * 提示 WBP 自己的布局决定（当前皮肤为固定 60 像素），支持逐条高度不同。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Stack",
            meta = (ClampMin = "0.0", UIMin = "0.0"))
  float RowGap = 8.0f;

  /** 堆叠容器（`VerticalBox`）距屏幕顶边的距离（像素）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Stack",
            meta = (ClampMin = "0.0", UIMin = "0.0"))
  float TopPadding = 40.0f;

  /**
   * 是否由 C++ 驱动 RenderOpacity 淡入淡出。
   *
   * 关掉后 C++ 完全不碰 RenderOpacity，由蓝图在 `BP_OnToastShown` /
   * `BP_OnToastFadeOutStarted` 里播放 WBP 动画；此时务必把
   * `FadeInDuration` / `FadeOutDuration` 调成与动画一致，队列节奏才对得上。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast|Fade")
  bool bUseNativeFade = true;

  // ================= 绑定控件 =================

  /** 提示文本（UMG 控件名必须是 `MessageText`）。 */
  UPROPERTY(meta = (BindWidget), Transient)
  TObjectPtr<UTextBlock> MessageText;

  /** 背景（可选，UMG 控件名必须是 `BackgroundBorder`）。 */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UBorder> BackgroundBorder;

  /** 图标（可选，UMG 控件名必须是 `IconImage`）。 */
  UPROPERTY(meta = (BindWidgetOptional), Transient)
  TObjectPtr<UImage> IconImage;

private:
  /** 用当前实例的默认样式补全一条提示（ShowToast 使用 / 静态入口复用）。 */
  FToastMessage BuildDefaultToast(const FText &Message) const;

  /** 用当前实例的默认图标补全一条指定颜色与时长的提示（ShowToastStyled 使用）。 */
  FToastMessage BuildStyledToast(const FText &Message, FLinearColor TextColor,
                                 FLinearColor BackgroundColor,
                                 float Duration) const;

  /** 把一条提示放进待显示队列；队列满时丢弃排队最久的一条。 */
  void EnqueueToast(const FToastMessage &Toast);

  /** 立即开始显示一条提示（调用方保证 Phase 已是 Idle）。 */
  void StartActiveToast(const FToastMessage &Toast);

  /** 淡入结束 -> 停留阶段。 */
  void EnterHoldPhase();

  /** 停留结束 -> 淡出阶段。 */
  void EnterFadeOutPhase();

  /**
   * 当前提示结束：收起控件，并按 bStartNext 决定是否立刻开始下一条。
   *
   * bStartNext=false 用于 ClearToasts：只收起当前提示，不启动队列里的下一条。
   */
  void FinishActiveToast(bool bStartNext);

  /** 把一条提示的文本 / 颜色 / 图标写进绑定控件。 */
  void ApplyToastVisuals(const FToastMessage &Toast);

  /** 记录并应用不透明度；bUseNativeFade 关闭时只记录、不写 RenderOpacity。 */
  void ApplyFadeOpacity(float NewOpacity);

  /** 解析实际停留时长：Duration <= 0 时回退到 DefaultDuration。 */
  float ResolveHoldDuration(const FToastMessage &Toast) const;

  /** 首次构造时检查绑定，缺 MessageText 记 ERROR，可选控件记 INFO。 */
  void WarnAboutMissingBindings();

  /** 待显示提示（按到达顺序）。 */
  UPROPERTY(Transient)
  TArray<FToastMessage> PendingToasts;

  /** 当前正在显示的提示（含图标引用，需 UPROPERTY 让 GC 可见）。 */
  UPROPERTY(Transient)
  FToastMessage ActiveToast;

  /** 当前播放阶段。 */
  EToastPhase Phase = EToastPhase::Idle;

  /** 当前阶段已经过的时间（秒）。 */
  float PhaseElapsedSeconds = 0.0f;

  /** 当前提示的停留时长（已解析过默认值）。 */
  float ActiveHoldDuration = 0.0f;

  /** 当前不透明度；Slate 重建时用来重放。 */
  float CurrentOpacity = 1.0f;

  /** 是否已进入 Slate 树（NativeConstruct / NativeDestruct 维护）。 */
  bool bIsConstructed = false;

  /** 绑定缺失警告只打一次，避免每次 Slate 重建都刷屏。 */
  bool bMissingBindingsWarned = false;
};
