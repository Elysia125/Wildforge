#include "UI/ToastStackWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Utils/WildforgeLog.h"

void UToastStackWidget::NativeOnInitialized() {
  Super::NativeOnInitialized();
  // 原生 UUserWidget 的 WidgetTree 此时已经创建好（UUserWidget::Initialize 里 new 的），
  // 在这里建树可以保证 AddToViewport 之前 GetToastContainer() 就能用。
  BuildWidgetTree();
}

TSharedRef<SWidget> UToastStackWidget::RebuildWidget() {
  // 兜底：如果 NativeOnInitialized 因为玩家上下文缺失没跑，Slate 控件重建时还有一次机会。
  BuildWidgetTree();
  return Super::RebuildWidget();
}

void UToastStackWidget::ConfigureLayout(float InTopPadding) {
  TopPadding = FMath::Max(0.0f, InTopPadding);
  BuildWidgetTree();

  if (!ToastContainerSlot) {
    WFLOG_WARNING("[ToastStackWidget] ConfigureLayout: 找不到 VerticalBox 的 "
                  "CanvasPanelSlot，顶部间距 %.1f 像素没生效",
                  TopPadding);
    return;
  }

  ToastContainerSlot->SetPosition(FVector2D(0.0f, TopPadding));
  WFLOG_INFO("[ToastStackWidget] 堆叠容器的顶部间距设为 %.1f 像素", TopPadding);
}

UVerticalBox *UToastStackWidget::GetToastContainer() {
  if (!ToastContainer) {
    BuildWidgetTree();
  }
  return ToastContainer;
}

void UToastStackWidget::BuildWidgetTree() {
  if (bTriedBuildWidgetTree) {
    return;
  }
  bTriedBuildWidgetTree = true;

  if (!WidgetTree) {
    WFLOG_ERROR("[ToastStackWidget] 控件树为空（WidgetTree 未初始化），"
                "无法创建提示堆叠容器");
    return;
  }

  // 理论上原生类走到这里时 RootWidget 一定是空的；万一是 WBP 子类，尽量复用它的根。
  if (WidgetTree->RootWidget) {
    RootCanvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
    ToastContainer = Cast<UVerticalBox>(
        WidgetTree->FindWidget(TEXT("ToastContainer")));
    if (!ToastContainer) {
      WFLOG_ERROR("[ToastStackWidget] 控件树已有根控件 %s，但找不到名为 "
                  "ToastContainer 的 VerticalBox；提示将无法显示",
                  *WidgetTree->RootWidget->GetName());
      return;
    }
    ToastContainerSlot = Cast<UCanvasPanelSlot>(ToastContainer->Slot);
    WFLOG_INFO("[ToastStackWidget] 复用控件树里已有的 ToastContainer（VerticalBox）");
    return;
  }

  RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
      UCanvasPanel::StaticClass(), TEXT("ToastRootCanvas"));
  ToastContainer = WidgetTree->ConstructWidget<UVerticalBox>(
      UVerticalBox::StaticClass(), TEXT("ToastContainer"));

  if (!RootCanvas || !ToastContainer) {
    WFLOG_ERROR("[ToastStackWidget] 创建 CanvasPanel / VerticalBox 失败"
                "（RootCanvas=%s，ToastContainer=%s）",
                *GetNameSafe(RootCanvas), *GetNameSafe(ToastContainer));
    return;
  }

  // 整个堆叠层都不吃鼠标：提示只是看的，点击要穿透到下面的游戏 / UI。
  RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  ToastContainer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

  ToastContainerSlot = RootCanvas->AddChildToCanvas(ToastContainer);
  if (ToastContainerSlot) {
    // 锚点 + 对齐都取「上边中点」：无论屏幕多宽，堆叠都保持水平居中。
    ToastContainerSlot->SetAnchors(FAnchors(0.5f, 0.0f));
    ToastContainerSlot->SetAlignment(FVector2D(0.5f, 0.0f));
    ToastContainerSlot->SetAutoSize(true);
    ToastContainerSlot->SetPosition(FVector2D(0.0f, TopPadding));
  } else {
    WFLOG_ERROR("[ToastStackWidget] 把 VerticalBox 加进根画布失败，"
                "顶部居中布局不会生效");
  }

  WidgetTree->RootWidget = RootCanvas;

  WFLOG_INFO("[ToastStackWidget] 已构建堆叠容器：根 CanvasPanel + 顶部居中 VerticalBox"
             "（幂等构建，%s）",
             ToastContainerSlot ? TEXT("槽已就绪") : TEXT("槽缺失"));
}
