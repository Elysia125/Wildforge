#include "ItemSystem/Components/StorageContainer.h"

UStorageContainer::UStorageContainer() {
  // 复制开关由基类构造函数设置（SetIsReplicatedByDefault(true)），这里只标类型。
  // 世界容器默认 OwnerOnly + 未指定拥有者 = 谁都打不开（安全默认值）；
  // 公共箱子请在蓝图类默认值 / 实例细节面板里显式改成 Public。
  setContainerType(EContainerType::WorldStorage);
}
