#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "ToastSettings.generated.h"

class UToastWidget;

/**
 * 提示（Toast）的项目设置。
 *
 * 位置：Project Settings > Project > Game > **提示（Toast）**；
 * 序列化到 `Config/DefaultGame.ini` 的 `[/Script/Wildforge.ToastSettings]` 段。
 *
 * 这里只放「用哪个控件类、挂在哪一层」这类**绑定信息**；
 * 停留时长 / 淡入淡出 / 队列上限等表现参数仍然在 Toast WBP 的类默认值上
 *（`UToastWidget` 的 `Toast|Timing` / `Toast|Queue` 分类），不要在 ini 里再抄一份。
 *
 * 纯客户端配置：`UWildforgeUIManager` 只在有本地玩家的端读取它。
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "提示（Toast）"))
class WILDFORGE_API UToastSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。 */
  static const UToastSettings *Get() { return GetDefault<UToastSettings>(); }

  /**
   * 提示控件类：必须是指向 `UToastWidget` 的可实例化 WBP 子类
   *（原生 `UToastWidget` 是 Abstract，不能直接实例化）。
   *
   * 用软类引用：第一次弹提示时才加载，没配提示的玩家不会白白加载这个资产。
   */
  UPROPERTY(Config, EditAnywhere, Category = "Toast",
            meta = (AllowedClasses = "/Script/Wildforge.ToastWidget"))
  TSoftClassPtr<UToastWidget> ToastWidgetClass;

  /**
   * 提示控件挂到视口的层级（越大越靠上）。
   *
   * 提示通常要盖在背包 / 宝箱窗口之上，所以默认给一个较大的值。
   * 只在提示控件**首次创建**时生效（UE 5.7 不支持直接改已挂控件的层级）。
   */
  UPROPERTY(Config, EditAnywhere, Category = "Toast",
            meta = (ClampMin = "0", UIMin = "0"))
  int32 ZOrder = 100;
};
