#pragma once

#include "CoreMinimal.h"

#include "ContainerAccessMode.generated.h"

/**
 * 容器访问权限模式（服务器权威，随容器复制）。
 *
 * 只回答「玩家 P 能不能打开 / 改动容器 C」，**不负责隐藏内容**：
 * `Slots` / `SlotOccupied` 是否发到某个客户端由容器的复制条件决定
 * （玩家背包 = 仅拥有者；世界容器 = 范围内所有客户端），见
 * `UItemContainer::GetContentsReplicationCondition()`。
 *
 * 权限判定顺序（`UItemContainer::CanBeAccessedBy_Implementation`）：
 *   - `OwnerOnly`：只有拥有者（`AccessOwner`，未指定时回退到容器所在 Pawn 的
 *     Controller）；
 *   - `Public`：任何玩家；
 *   - `Whitelist`：拥有者 + `AllowedPlayers`；
 *   - `Locked`：任何人都不能访问（用于剧情锁 / 任务锁）。
 *
 * 新增模式时记得同步 `CanBeAccessedBy_Implementation` 的 `switch`，否则会落到
 * `default` 分支 = 拒绝访问（安全默认值）。
 */
UENUM(BlueprintType)
enum class EContainerAccessMode : uint8 {
  /** 只有拥有者可以访问；没有拥有者的世界容器默认谁都不能用。 */
  OwnerOnly UMETA(DisplayName = "仅拥有者"),

  /** 任何玩家都可以访问（公共宝箱 / 掉落箱）。 */
  Public UMETA(DisplayName = "公开"),

  /** 拥有者 + 白名单里的玩家可以访问（队伍仓库 / 合租储物箱）。 */
  Whitelist UMETA(DisplayName = "白名单"),

  /** 任何人都不能访问（未解锁 / 剧情锁定）。 */
  Locked UMETA(DisplayName = "锁定"),
};
