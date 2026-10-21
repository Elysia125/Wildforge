#pragma once

#include "CoreMinimal.h"

#include "ItemSystem/Components/ItemContainer.h"

#include "StorageContainer.generated.h"

/**
 * 世界容器（宝箱 / 储物箱 / 掉落箱 / 场景里的固定容器）。
 *
 * ## 与 UItemContainer / UPlayerInventory 的区别
 *
 * 只差一件事：**内容数组复制给所有相关客户端**（`COND_None`），而不是只发给
 * 拥有者（`COND_OwnerOnly`）。原因：世界箱子是关卡 actor / 服务器生成的 actor，
 * 没有客户端 PlayerController 作为 owning connection；如果沿用 `COND_OwnerOnly`，
 * 没有任何客户端会收到 `Slots`，箱子 UI 永远是空的。
 *
 * 复制条件只能按**类**选（`GetLifetimeReplicatedProps` 只在 CDO 上调用一次，
 * 见 `UItemContainer::GetContentsReplicationCondition()` 的注释），所以这里用
 * 子类 override 而不是实例开关。
 *
 * ## 安全边界（重要）
 *
 * 内容对范围内所有客户端可见，这是「别人也能看到箱子里的东西」的显示所必需的；
 * 权限（`AccessMode` / `AccessOwner` / `AllowedPlayers`）负责门控**交互**：
 * 没有权限的玩家不能打开、不能拖拽、不能发转移 RPC（服务器在交互组件里重新校验）。
 * 如果以后要求「连内容都不能让没权限的客户端看到」，那需要按连接自定义复制，
 * 不能只靠这个类。
 *
 * ## 使用方式
 *
 * 1. 在宝箱 Actor 上加一个 `UStorageContainer` 组件（或做一个它的蓝图子类）；
 * 2. 宝箱 Actor 必须 `bReplicates = true`（组件构造函数已
 *    `SetIsReplicatedByDefault(true)`）；
 * 3. 按用途设置权限：
 *    - 掉落箱 / 公共宝箱：`AccessMode = Public`；
 *    - 玩家私有储物箱：`AccessMode = OwnerOnly` + 服务器调用
 *      `SetAccessOwnerController(PlayerController)`（或在生成时
 *      `SetOwner(PlayerController)`）；
 *    - 队伍仓库：`AccessMode = Whitelist` + `AddAllowedPlayer(PlayerState)`；
 *    - 未解锁：`Locked`。
 *
 * 默认是 `OwnerOnly` 且没有拥有者 ⇒ **谁都打不开**。这是安全默认值：
 * 与其让一个忘了配置的箱子变成公共物资，不如让它先打不开，由策划显式改权限。
 *
 * ## 宝箱等级 / 掉落 / 开箱表现不在这里
 *
 * 这些是**宝箱 Actor**（`AChestActor`）的职责：等级决定掉落稀有度池、开箱
 * 动画与窗口，而本组件只负责「存储 + 复制 + 权限」。这样尸体、熔炉、掉落箱
 * 等其它世界容器可以复用同一个组件，不会被宝箱专用字段污染。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
       BlueprintType, Blueprintable)
class WILDFORGE_API UStorageContainer : public UItemContainer {
  GENERATED_BODY()

public:
  UStorageContainer();

protected:
  // 世界容器：内容发给所有相关客户端（否则没有 owning connection 的箱子
  // 谁都收不到内容，UI 无法显示）。见类注释的安全边界。
  virtual ELifetimeCondition GetContentsReplicationCondition() const override {
    return COND_None;
  }
};
