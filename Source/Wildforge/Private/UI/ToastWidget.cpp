#include "UI/ToastWidget.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Styling/SlateColor.h"
#include "Utils/WildforgeLog.h"

namespace {
/**
 * 取提示控件类的类默认值对象（读默认样式 / 堆叠参数用）。
 *
 * 类为空或不是 UToastWidget 子类时回退到原生基类 CDO——调用方只会传配置好的
 * WBP 子类，这个兜底只是避免空指针把一次提示变成崩溃。
 */
const UToastWidget *ResolveToastDefaults(const UClass *ToastClass) {
  if (ToastClass && ToastClass->IsChildOf(UToastWidget::StaticClass())) {
    if (const UToastWidget *ClassDefaults =
            Cast<UToastWidget>(ToastClass->GetDefaultObject())) {
      return ClassDefaults;
    }
  }
  return GetDefault<UToastWidget>();
}
} // namespace

FToastMessage UToastWidget::BuildDefaultToast(const FText &Message) const {
  FToastMessage Toast;
  Toast.Message = Message;
  Toast.TextColor = DefaultTextColor;
  Toast.BackgroundColor = DefaultBackgroundColor;
  Toast.Icon = DefaultIcon;
  Toast.IconTint = DefaultIconTint;
  // <= 0 表示「用 DefaultDuration」，由 ShowToastMessage 统一解析。
  Toast.Duration = 0.0f;
  return Toast;
}

FToastMessage UToastWidget::BuildStyledToast(const FText &Message,
                                             FLinearColor TextColor,
                                             FLinearColor BackgroundColor,
                                             float Duration) const {
  FToastMessage Toast;
  Toast.Message = Message;
  Toast.TextColor = TextColor;
  Toast.BackgroundColor = BackgroundColor;
  Toast.Icon = DefaultIcon;
  Toast.IconTint = DefaultIconTint;
  Toast.Duration = Duration;
  return Toast;
}

FToastMessage
UToastWidget::MakeDefaultToastMessage(const UClass *ToastClass,
                                      const FText &Message) {
  return ResolveToastDefaults(ToastClass)->BuildDefaultToast(Message);
}

FToastMessage UToastWidget::MakeStyledToastMessage(
    const UClass *ToastClass, const FText &Message, FLinearColor TextColor,
    FLinearColor BackgroundColor, float Duration) {
  return ResolveToastDefaults(ToastClass)
      ->BuildStyledToast(Message, TextColor, BackgroundColor, Duration);
}

void UToastWidget::ShowToast(const FText &Message) {
  ShowToastMessage(BuildDefaultToast(Message));
}

void UToastWidget::ShowToastStyled(const FText &Message, FLinearColor TextColor,
                                   FLinearColor BackgroundColor,
                                   float Duration) {
  ShowToastMessage(
      BuildStyledToast(Message, TextColor, BackgroundColor, Duration));
}

void UToastWidget::ShowToastMessage(const FToastMessage &Message) {
  if (Message.Message.IsEmpty()) {
    WFLOG_WARNING("[Toast] %s 收到空文本提示，已忽略。",
                  *GetClass()->GetName());
    return;
  }

  // 把「Duration <= 0 表示使用默认时长」在这里解析掉，
  // 后续状态机与 GetActiveToast() 拿到的都是有效值。
  FToastMessage NewToast = Message;
  NewToast.Duration = ResolveHoldDuration(NewToast);

  // 相同文本合并：典型场景是按住冲刺时服务器每秒发一次「精力不足」。
  // 直接排队会刷出一长串重复提示（队列满了还会把别的提示挤掉），
  // 这里改成：正在显示的那条只重置停留计时，或替换队列尾部的同文本条目。
  // FadeOut 阶段不合并——那条马上就要消失，让它正常收尾、下一条重新显示。
  if (bIsConstructed && Phase != EToastPhase::Idle &&
      Phase != EToastPhase::FadeOut &&
      ActiveToast.Message.EqualTo(NewToast.Message)) {
    const float PreviousDuration = ActiveHoldDuration;
    ActiveHoldDuration = NewToast.Duration;
    ActiveToast = NewToast;

    if (Phase == EToastPhase::Hold) {
      PhaseElapsedSeconds = 0.0f;
      ApplyToastVisuals(ActiveToast);
    }

    WFLOG_INFO("[Toast] 相同提示正在显示，停留计时重置为 %.2fs"
               "（原 %.2fs，不重复排队）：%s",
               ActiveHoldDuration, PreviousDuration,
               *ActiveToast.Message.ToString());
    return;
  }

  if (PendingToasts.Num() > 0 &&
      PendingToasts.Last().Message.EqualTo(NewToast.Message)) {
    PendingToasts.Last() = NewToast;
    WFLOG_INFO("[Toast] 队列尾部已有相同提示，替换为最新样式"
               "（待显示 %d 条）：%s",
               PendingToasts.Num(), *NewToast.Message.ToString());
    return;
  }

  // 控件还没进入 Slate 树时先排队：NativeConstruct 里再开始播放，
  // 避免蓝图动画在控件树尚未就绪时被 BP_OnToastShown 触发。
  if (!bIsConstructed) {
    EnqueueToast(NewToast);
    WFLOG_INFO("[Toast] %s 尚未构造，提示先入队（待显示 %d 条）：%s",
               *GetClass()->GetName(), PendingToasts.Num(),
               *NewToast.Message.ToString());
    return;
  }

  if (Phase == EToastPhase::Idle) {
    StartActiveToast(NewToast);
    return;
  }

  EnqueueToast(NewToast);
  WFLOG_INFO("[Toast] 当前提示尚未结束，新提示入队（待显示 %d 条）：%s",
             PendingToasts.Num(), *NewToast.Message.ToString());
}

void UToastWidget::EnqueueToast(const FToastMessage &Toast) {
  // MaxQueuedMessages <= 0 表示不限制；队列满时丢弃排队最久的一条，
  // 让最新信息尽量能被看到（当前正在显示的那条不受影响）。
  if (MaxQueuedMessages > 0 && PendingToasts.Num() >= MaxQueuedMessages) {
    const FToastMessage DroppedToast = PendingToasts[0];
    PendingToasts.RemoveAt(0);
    WFLOG_WARNING("[Toast] 待显示队列已满（上限 %d 条），丢弃排队最久的一条：%s",
                  MaxQueuedMessages, *DroppedToast.Message.ToString());
  }

  PendingToasts.Add(Toast);
}

void UToastWidget::ClearToasts(bool bClearActive) {
  const int32 DroppedCount = PendingToasts.Num();
  PendingToasts.Empty();

  if (Phase != EToastPhase::Idle) {
    if (bClearActive) {
      // FinishActiveToast 会广播 BP_OnToastHidden，
      // 并在队列为空时广播 BP_OnToastQueueEmptied。
      FinishActiveToast(false);
    }
    // bClearActive=false：当前提示自然播完，队列已清空，
    // 它结束时同样会广播 QueueEmptied。
  } else {
    SetVisibility(ESlateVisibility::Collapsed);
    if (DroppedCount > 0 && bIsConstructed) {
      BP_OnToastQueueEmptied();
    }
  }

  WFLOG_INFO("[Toast] 清空提示：丢弃待显示 %d 条，清除当前提示=%s。",
             DroppedCount, bClearActive ? TEXT("是") : TEXT("否"));
}

void UToastWidget::NativeConstruct() {
  Super::NativeConstruct();
  bIsConstructed = true;

  WarnAboutMissingBindings();

  // Slate 控件可能被释放并重建：当前提示的状态要按记录重放一遍，
  // 不能依赖「构造 / 灌数据」的先后顺序（与槽位控件同一类教训）。
  if (Phase != EToastPhase::Idle) {
    ApplyToastVisuals(ActiveToast);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    ApplyFadeOpacity(CurrentOpacity);
    WFLOG_INFO("[Toast] 控件重建，重放当前提示（阶段=%d，不透明度=%.2f）：%s",
               static_cast<int32>(Phase), CurrentOpacity,
               *ActiveToast.Message.ToString());
    return;
  }

  SetVisibility(ESlateVisibility::Collapsed);
  if (bUseNativeFade) {
    CurrentOpacity = 1.0f;
    SetRenderOpacity(CurrentOpacity);
  }

  // 控件构造前收到、被暂存在队列里的提示，从这里开始播第一条。
  if (PendingToasts.Num() > 0) {
    const FToastMessage NextToast = PendingToasts[0];
    PendingToasts.RemoveAt(0);
    StartActiveToast(NextToast);
  }
}

void UToastWidget::NativeDestruct() {
  bIsConstructed = false;
  Super::NativeDestruct();
}

void UToastWidget::NativeTick(const FGeometry &MyGeometry, float InDeltaTime) {
  Super::NativeTick(MyGeometry, InDeltaTime);

  if (!bIsConstructed || Phase == EToastPhase::Idle) {
    return;
  }

  PhaseElapsedSeconds += InDeltaTime;

  switch (Phase) {
  case EToastPhase::FadeIn: {
    if (FadeInDuration <= KINDA_SMALL_NUMBER) {
      ApplyFadeOpacity(1.0f);
      EnterHoldPhase();
      break;
    }

    const float Alpha =
        FMath::Clamp(PhaseElapsedSeconds / FadeInDuration, 0.0f, 1.0f);
    ApplyFadeOpacity(Alpha);
    if (Alpha >= 1.0f) {
      EnterHoldPhase();
    }
    break;
  }
  case EToastPhase::Hold:
    if (PhaseElapsedSeconds >= ActiveHoldDuration) {
      EnterFadeOutPhase();
    }
    break;
  case EToastPhase::FadeOut: {
    if (FadeOutDuration <= KINDA_SMALL_NUMBER) {
      FinishActiveToast(true);
      break;
    }

    const float Alpha =
        1.0f - FMath::Clamp(PhaseElapsedSeconds / FadeOutDuration, 0.0f, 1.0f);
    ApplyFadeOpacity(Alpha);
    if (Alpha <= 0.0f) {
      FinishActiveToast(true);
    }
    break;
  }
  default:
    break;
  }
}

void UToastWidget::StartActiveToast(const FToastMessage &Toast) {
  ActiveToast = Toast;
  ActiveHoldDuration = ResolveHoldDuration(Toast);
  ActiveToast.Duration = ActiveHoldDuration;
  Phase = EToastPhase::FadeIn;
  PhaseElapsedSeconds = 0.0f;

  ApplyToastVisuals(ActiveToast);
  // Toast 不参与点击判定，避免挡住 HUD 下方的 UI。
  SetVisibility(ESlateVisibility::HitTestInvisible);

  if (FadeInDuration <= KINDA_SMALL_NUMBER) {
    ApplyFadeOpacity(1.0f);
    EnterHoldPhase();
  } else {
    ApplyFadeOpacity(0.0f);
  }

  if (!bUseNativeFade) {
    // WBP 动画模式：C++ 不驱动 RenderOpacity，视觉完全由 BP_OnToastShown /
    // BP_OnToastFadeOutStarted 里播放的动画负责。
    //
    // 上一次淡出可能把不透明度留在 0，而这一次淡入若没有从头重播（动画播放器
    // 状态残留、动画被其它逻辑打断），提示就会「已经显示但看不见」——这正是
    // 「有时候不显示提示」的一种来源。这里先停掉残留动画并把不透明度复位成
    // 1：淡入动画正常播放时会在下一帧覆盖它；动画缺失时至少能看见提示。
    StopAllAnimations();
    SetRenderOpacity(1.0f);
  }

  WFLOG_INFO("[Toast] 开始显示提示（待显示 %d 条，停留 %.2fs，淡入 %.2fs，"
             "淡出 %.2fs，原生淡入淡出=%s）：%s",
             PendingToasts.Num(), ActiveHoldDuration, FadeInDuration,
             FadeOutDuration,
             bUseNativeFade ? TEXT("开") : TEXT("关（WBP 动画）"),
             *ActiveToast.Message.ToString());

  if (bIsConstructed) {
    BP_OnToastShown(ActiveToast);
  }
}

void UToastWidget::EnterHoldPhase() {
  Phase = EToastPhase::Hold;
  PhaseElapsedSeconds = 0.0f;
  ApplyFadeOpacity(1.0f);
}

void UToastWidget::EnterFadeOutPhase() {
  Phase = EToastPhase::FadeOut;
  PhaseElapsedSeconds = 0.0f;

  WFLOG_INFO("[Toast] 提示进入淡出：%s", *ActiveToast.Message.ToString());

  // 蓝图可以在这里播放自己的淡出动画（配合 bUseNativeFade=false）。
  if (bIsConstructed) {
    BP_OnToastFadeOutStarted(ActiveToast);
  }
}

void UToastWidget::FinishActiveToast(bool bStartNext) {
  const FToastMessage FinishedToast = ActiveToast;

  Phase = EToastPhase::Idle;
  PhaseElapsedSeconds = 0.0f;
  ActiveHoldDuration = 0.0f;
  ApplyFadeOpacity(0.0f);
  ActiveToast = FToastMessage();

  WFLOG_INFO("[Toast] 提示结束：%s（待显示 %d 条）。",
             *FinishedToast.Message.ToString(), PendingToasts.Num());

  if (bIsConstructed) {
    BP_OnToastHidden(FinishedToast);
  }

  // 蓝图可能在 BP_OnToastHidden 里又调 ShowToast 弹了一条：
  // 此时让它先播，排队中的下一条继续留在队列里，避免被这里覆盖。
  if (Phase != EToastPhase::Idle) {
    return;
  }

  if (bStartNext && PendingToasts.Num() > 0) {
    const FToastMessage NextToast = PendingToasts[0];
    PendingToasts.RemoveAt(0);
    StartActiveToast(NextToast);
    return;
  }

  SetVisibility(ESlateVisibility::Collapsed);

  if (bIsConstructed && PendingToasts.Num() == 0) {
    BP_OnToastQueueEmptied();
  }

  // 控件真正回到空闲：通知 C++ 监听者（多提示堆叠器据此移除控件、上移其余提示并补位）。
  // 上面两个提前 return 的分支（蓝图又弹了一条 / 内部队列接着播）不算空闲，不广播。
  OnToastFinished.Broadcast(this, FinishedToast);
}

void UToastWidget::ApplyToastVisuals(const FToastMessage &Toast) {
  if (MessageText) {
    MessageText->SetText(Toast.Message);
    MessageText->SetColorAndOpacity(FSlateColor(Toast.TextColor));
  }

  if (BackgroundBorder) {
    BackgroundBorder->SetBrushColor(Toast.BackgroundColor);
  }

  if (IconImage) {
    if (Toast.Icon) {
      // bMatchSize=false：图标尺寸由 WBP 设计器控制，不被纹理原始尺寸顶掉。
      IconImage->SetBrushFromTexture(Toast.Icon, false);
      IconImage->SetColorAndOpacity(Toast.IconTint);
      IconImage->SetVisibility(ESlateVisibility::HitTestInvisible);
    } else {
      IconImage->SetVisibility(ESlateVisibility::Collapsed);
    }
  }
}

void UToastWidget::ApplyFadeOpacity(float NewOpacity) {
  CurrentOpacity = FMath::Clamp(NewOpacity, 0.0f, 1.0f);

  // bUseNativeFade 关闭时把 RenderOpacity 完全交给蓝图动画，
  // C++ 只记录 CurrentOpacity 供 Slate 重建时参考。
  if (bUseNativeFade) {
    SetRenderOpacity(CurrentOpacity);
  }
}

float UToastWidget::ResolveHoldDuration(const FToastMessage &Toast) const {
  const float Requested =
      Toast.Duration > 0.0f ? Toast.Duration : DefaultDuration;
  return FMath::Max(0.0f, Requested);
}

void UToastWidget::WarnAboutMissingBindings() {
  if (bMissingBindingsWarned) {
    return;
  }
  bMissingBindingsWarned = true;

  if (!MessageText) {
    WFLOG_ERROR("[Toast] %s 没有绑定名为 MessageText 的 UTextBlock；"
                "提示文本不会显示。",
                *GetClass()->GetName());
  }
  if (!BackgroundBorder) {
    WFLOG_INFO("[Toast] %s 未绑定 BackgroundBorder（可选）；"
               "背景颜色不会生效。",
               *GetClass()->GetName());
  }
  if (!IconImage) {
    WFLOG_INFO("[Toast] %s 未绑定 IconImage（可选）；图标不会显示。",
               *GetClass()->GetName());
  }
}
