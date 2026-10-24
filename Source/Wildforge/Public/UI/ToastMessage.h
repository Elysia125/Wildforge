#pragma once

#include "CoreMinimal.h"

#include "ToastMessage.generated.h"

class UTexture2D;

/**
 * 一条 Toast 提示的完整数据。
 *
 * 由 `UToastWidget::ShowToastMessage` 接收。只想弹纯文本时用
 * `UToastWidget::ShowToast` / `ShowToastStyled` 即可，不必手动拼这个结构体。
 *
 * 所有字段都只影响**表现**（客户端本地），不参与服务器权威状态。
 */
USTRUCT(BlueprintType)
struct FToastMessage {
  GENERATED_BODY()

  /** 提示文本。为空时 `UToastWidget` 会记一条 WARNING 并忽略这条消息。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast")
  FText Message;

  /** 文本颜色。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast")
  FLinearColor TextColor = FLinearColor::White;

  /** 背景颜色（需要 WBP 里绑定名为 `BackgroundBorder` 的 UBorder）。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast")
  FLinearColor BackgroundColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.75f);

  /** 可选图标（需要 WBP 里绑定名为 `IconImage` 的 UImage）。为空时自动折叠图标。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast")
  TObjectPtr<UTexture2D> Icon = nullptr;

  /** 图标着色。 */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast")
  FLinearColor IconTint = FLinearColor::White;

  /**
   * 完全显示（不透明度 = 1）的停留时长，单位秒。
   *
   * <= 0 表示使用 `UToastWidget::DefaultDuration`。
   * 一条提示的总时长 = 淡入时长 + 停留时长 + 淡出时长。
   */
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Toast",
            meta = (ClampMin = "0.0"))
  float Duration = 0.0f;
};
