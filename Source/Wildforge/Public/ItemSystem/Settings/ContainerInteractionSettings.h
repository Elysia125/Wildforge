#pragma once

#include "CoreMinimal.h"

#include "Engine/DeveloperSettings.h"

#include "ContainerInteractionSettings.generated.h"

/**
 * 容器交互（`UContainerInteractionComponent`）的可调数值。
 *
 * 位置：Project Settings > Project > Game > **容器交互（Container Interaction）**；
 * 序列化到 `Config/DefaultGame.ini` 的
 * `[/Script/Wildforge.ContainerInteractionSettings]` 段。
 *
 * ⚠️ **只有权威端读它**：服务器在组件的 `BeginPlay` 里读 ini、夹紧后写入
 * `UContainerInteractionComponent::MaxInteractionDistance`，再通过
 * `COND_InitialOnly` 复制给客户端。客户端本地那份 ini 是玩家可改的文件，
 * 绝不能拿它驱动玩法（否则改 ini 就能隔空开箱）。
 */
UCLASS(Config = Game, DefaultConfig,
       meta = (DisplayName = "容器交互（Container Interaction）"))
class WILDFORGE_API UContainerInteractionSettings : public UDeveloperSettings {
  GENERATED_BODY()

public:
  /** 取 CDO（ini 的值只在这里读得到）。客户端不要拿它驱动玩法，见类注释。 */
  static const UContainerInteractionSettings *Get() {
    return GetDefault<UContainerInteractionSettings>();
  }

  /** 是否用 ini 的值覆盖组件上的类默认值。 */
  UPROPERTY(Config, EditAnywhere, Category = "ContainerInteraction",
            meta = (InlineEditConditionToggle))
  bool bOverride_MaxInteractionDistance = false;

  /**
   * 玩家与容器之间的最大交互距离（厘米，3D 距离）。
   *
   * 玩家自己的背包 / 快捷栏不受距离限制（容器就在自己身上）；世界容器
   * （宝箱 / 储物箱）用它判定。0 = 只有贴在容器上才能交互。
   */
  UPROPERTY(Config, EditAnywhere, Category = "ContainerInteraction",
            meta = (EditCondition = "bOverride_MaxInteractionDistance",
                    ClampMin = "0.0", UIMax = "2000.0"))
  float MaxInteractionDistance = 250.f;
};
