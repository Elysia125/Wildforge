# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-10-06T14:30:00.000Z
> Files: 22 tracked | Anatomy hits: 0 | Misses: 0

> **本文件只索引源码与配置**（Source/、Config/、根目录构建文件）。UE 项目的 Content/
> 是二进制资产（.uasset/.umap），按文件大小估出来的 token 数没有导航价值，已从
> `.wolf/config.json` 的 `anatomy.exclude_patterns` 中排除，并手工剪掉了历史条目。
> 需要了解资产请直接在编辑器里看；架构说明以 `CLAUDE.md` 的“架构”节为准（人写、更准确）。

## ./

- `.clang-format` — 基于 Unreal Engine 风格的配置 (~116 tok)
- `.clangd` — UBT 的 -Mode=GenerateClangDatabase 以 clang-cl（CL 驱动）生成 compile_commands.json： (~101 tok)
- `.gitignore` — Git ignore rules（含 CLAUDE.LOCAL.md，本机专属文件不入库） (~80 tok)
- `.ignore` (~30 tok)
- `CLAUDE.md` — 项目级指令与强制约束（跨成员通用，入库）：项目概览 / 构建命令 / 架构 / 约定与坑 / 上下文文件分工（buglog↔cerebrum 写法约定）/ 强制约束 (~3800 tok)
- `CLAUDE.LOCAL.md` — 本机专属：引擎/项目/LLVM 路径与编译验证命令（不入 git） (~900 tok)
- `README.md` — Project documentation (~11 tok)
- `Wildforge.code-workspace` (~4460 tok)
- `Wildforge.uproject` (~124 tok)

## Config/

- `DefaultEditor.ini` (~14878 tok)
- `DefaultEditorPerProjectUserSettings.ini` (~19 tok)
- `DefaultEngine.ini` (~942 tok)
- `DefaultGame.ini` — 含 UItemSystemSettings 的 ItemTable 路径（Project Settings 序列化到此） (~36 tok)
- `DefaultInput.ini` (~3677 tok)

## Source/Wildforge/

- `Wildforge.Build.cs` — 模块构建规则：PublicDependencyModuleNames = Core, CoreUObject, Engine, InputCore, UMG, Slate, SlateCore, DeveloperSettings（Enhanced Input 未列出，靠引擎传递依赖） (~500 tok)
- `Wildforge.cpp` — FWildforgeModule（FDefaultGameModuleImpl 子类）：Startup/ShutdownModule 注册 UWildforgeLog 文件输出设备 (~400 tok)
- `Wildforge.h` — 模块声明头（PrimaryGameModuleHeader） (~100 tok)

## Source/Wildforge/Public/Character/

- `BaseCharacter.h` — ABaseCharacter（ACharacter 子类）：bReplicates=true，持有 UCharacterAttributes 组件 (~600 tok)

## Source/Wildforge/Private/Character/

- `BaseCharacter.cpp` — ABaseCharacter 实现：构造函数创建 UCharacterAttributes 组件 (~300 tok)

## Source/Wildforge/Public/Character/Components/

- `CharacterAttributes.h` — UCharacterAttributes：复制 Health/MaxHealth（ReplicatedUsing OnRep 广播 FOnHealthChanged/FOnMaxHealthChanged），Setter 为 BlueprintAuthorityOnly，且 SetHealth/SetMaxHealth 内联加非权威端门禁（IsAuthoritativeForActorComponent + WFLOG_ERROR） (~900 tok)

## Source/Wildforge/Private/Character/Components/

- `CharacterAttributes.cpp` — 属性组件实现：DOREPLIFETIME + OnRep 广播委托，SetHealth/SetMaxHealth 带 Clamp (~900 tok)

## Source/Wildforge/Public/Character/Player/

- `PlayerCharacter.h` — APlayerCharacter：持有 UPlayerInventory（30 格）；GetInventory() (~700 tok)

## Source/Wildforge/Private/Character/Player/

- `PlayerCharacter.cpp` — APlayerCharacter 实现：构造函数创建 UPlayerInventory 并 InitializeContainer(30) (~600 tok)

## Source/Wildforge/Public/Core/

- `WildforgePlayerController.h` — AWildforgePlayerController：ShowInventory()/HideInventory 切换 FInputModeGameAndUI ↔ GameOnly（光标显隐） (~500 tok)

## Source/Wildforge/Private/Core/

- `WildforgePlayerController.cpp` — 控制器实现：主界面 Widget 创建与显示/隐藏切换 (~700 tok)

## Source/Wildforge/Public/ItemSystem/Actors/

- `ItemMaster.h` — AItemMaster：物品 Actor 桩（暂空），被 FItemInformation::ItemClass 引用 (~200 tok)

## Source/Wildforge/Private/ItemSystem/Actors/

- `ItemMaster.cpp` — AItemMaster 实现（空） (~80 tok)

## Source/Wildforge/Public/ItemSystem/Enums/

- `ContainerType.h` — EContainerType 枚举（PlayerStorage/PlayerInventory/…） (~200 tok)
- `ItemArmor.h` — EItemArmor 枚举 (~150 tok)
- `ItemRarity.h` — EItemRarity 枚举（Common…） (~150 tok)
- `ItemType.h` — EItemType 枚举（Other/…） (~200 tok)

## Source/Wildforge/Public/ItemSystem/Structs/

- `ItemInfo.h` — FItemInformation（FTableRowBase，需 include Engine/DataTable.h——别靠 SharedPCH，见 bug-014）：ItemID/ItemName/ItemDesc/ItemQuality（兼作堆叠数量）/ItemDamage/IsStackable/MaxStackSize/ItemCurHP/ItemMaxHP/ItemIcon/ItemType/ItemRarity/ItemArmor/ItemClass/UseAmmo/Ammo/AmmoMax (~900 tok)

## Source/Wildforge/Public/ItemSystem/Database/

- `ItemDatabaseSubsystem.h` — 物品定义数据库（GameInstanceSubsystem）：单一真相源，从项目设置取表 + int 键懒缓存 (~900 tok)
- `ItemSystemSettings.h` — UDeveloperSettings（Project Settings > Game > Item System）：TSoftObjectPtr<UDataTable> ItemTable，配置驱动物品表 (~500 tok)

## Source/Wildforge/Private/ItemSystem/Database/

- `ItemDatabaseSubsystem.cpp` — 子系统实现：Initialize 从 UItemSystemSettings 取 ItemTable 并 LoadSynchronous，GetRowMap 构建 int 键缓存；日志走 WFLOG_* (~1200 tok)
- `ItemSystemSettings.cpp` — UItemSystemSettings 实现（仅包含头，无额外逻辑） (~60 tok)

## Source/Wildforge/Public/Utils/

- `WildforgeLog.h` — 日志工具（UBlueprintFunctionLibrary）：Info/Warning/Error/WFLOG_* 宏；注册接口 WildforgeLogging::RegisterFileOutput；文件输出走异步设备 (FAsyncWriter) (~1000 tok)
- `WildforgeAuthority.h` — 权威端判断辅助（inline）：IsAuthoritativeForActorComponent(UActorComponent*) = GetOwner()->HasAuthority()；供组件子类使用（HasAuthority 是 AActor 的方法，组件上没有，直接写会 C3861） (~350 tok)

## Source/Wildforge/Private/Utils/

- `WildforgeLog.cpp` — 实现：FWildforgeLogDevice（FOutputDevice）按等级分派到 3 个 FLevelFileWriter（各含 FAsyncWriter 后台线程 + 独立锁），UTF-8 无 BOM，跨日切目录；仅此处允许内部 UE_LOG(LogWildforge, …) (~1600 tok)

## Source/Wildforge/Public/ItemSystem/Components/

- `ItemContainer.h` — 服务器权威物品容器组件：复制 Slots/SlotOccupied + 派生索引(FreeSlots/ItemIDToSlot/UsedCount)；增/删(AddItemStack/AddItemByID；RemoveItem 按槽位、RemoveAllItem 按 ItemID，均带 Quantity 默认 -1=全部)/整理/调整大小/查询 API；MoveOrMergeItem(From,To) 提供拖拽「放置」语义（同 ItemID 可堆叠且目标未满→合并、余数留源槽，否则交换）；OnContainerChanged 委托 + Server_* RPC（含 Server_MoveOrMerge） (~2900 tok)

## Source/Wildforge/Private/ItemSystem/Components/

- `ItemContainer.cpp` — 容器实现：RebuildDerivedState 由复制数据重建索引；AddItemStack 按堆叠放置；RemoveSlotQuantityInternal(单槽)/RemoveByItemIDInternal(跨槽)按数量逐格扣除；MoveOrMergeItem 合并/交换（合并只在源与目标两槽间重分配数量：目标加满、余数留源，源清空则回收槽位）；OrganizeContainer 合并/排序；顶部 WF_CONTAINER_AUTHORITY_GUARD 宏给 10 个修改函数加非权威端门禁；日志统一 WFLOG_*；Server_* RPC 转调权威方法 (~4000 tok)

## Source/Wildforge/Public/ItemSystem/UI/

- `ItemContainerGrid.h` — 背包网格 Widget（UniformGridPanel+ScrollBox）：InitializeGrid 绑定容器并订阅 OnContainerChanged（**只处理结构**：容量变化才增删槽位/重排），RefreshGrid 为全量重建入口；Bind/UnbindToContainer；EnsureSlotCount（返回数量是否变化）/LayoutSlots；槽位内容由 UInventorySlotWidget 自己拉 (~1200 tok)
- `InventoryUserWidget.h` — 背包主界面：FInventoryThemeConfig 主题结构体；InitializeInventory/ShowInventoryPage/ShowCraftPage；BindWidget 按钮/Tab/WidgetSwitcher/ItemContainerGrid/动画；主题美化 ApplyInitialThemeStyles (~2200 tok)
- `InventorySlotWidget.h` — 单格 Widget。**对外只留「来源」与控件自身状态**：InitializeSlot(容器,索引)/UnbindFromContainer/RefreshFromContainer + GetOwningContainer/GetSlotIndex，SetSelected/SetSlotStyle，以及可被蓝图覆写的 SetHighlight（BlueprintNativeEvent：C++ 默认染色 + HighlightColor）；**SetItemData(只收结构体)/ClearSlot/SetQuantity/SetItemHP/SetTopText/SetBottomText/SetItemIcon/SetItemStyle 都是 private 渲染细节**（数量唯一来源是 Item.ItemQuality，没有 CurrentQuantity）；HandleContainerChanged 处理容器广播；BindWidget TopText/BottomText/QuantityText/ItemHP/SlotStyle/ItemStyle/OverlayRoot/SizeBoxRoot；拖拽回调（NativeDestruct 退订 + 兜底复位不透明度）；状态 CurrentItem/bHasItemData + RefreshFromState()/IsSameAsCurrentData()；蓝图钩子 OnItemDataSet(结构体) (~2400 tok)
- `InventorySlotDragDropOperation.h` — 拖拽操作（UDragDropOperation）：SourceSlotIndex/SourceContainerType/ItemInfo/SourceContainer + TWeakObjectPtr<UUserWidget> SourceWidget（弱引用避免源控件被网格刷新销毁后悬空）与 RestoreSourceWidget() 兜底复位，供 NativeOnDrop 判定来源并恢复显示 (~700 tok)

## Source/Wildforge/Private/ItemSystem/UI/

- `ItemContainerGrid.cpp` — 网格实现：NativeConstruct 从拥有者 Pawn 查找 UItemContainer 并 InitializeGrid；HandleContainerChanged 只做结构（容量变了才 EnsureSlotCount+LayoutSlots，不整表刷内容）；RefreshGrid 为全量入口（保证数量/布局 + 逐格 InitializeSlot 重绑并拉数据）；EnsureSlotCount 补建时当场 InitializeSlot、移除时先 UnbindFromContainer (~1600 tok)
- `InventoryUserWidget.cpp` — 主界面实现：NativeConstruct 取容器 + 初始化网格；Tab 点击切 WidgetSwitcher 页并更新高亮/反馈动画；整理按钮调 Server_OrganizeContainer（RPC，不可直接调 OrganizeContainer）；ApplyInitialThemeStyles 应用配色与阴影 (~2600 tok)
- `InventorySlotWidget.cpp` — 单格实现：InitializeSlot 绑定 (容器,索引) 并退订/重订 + 首次拉取，HandleContainerChanged→RefreshFromContainer（GetItemAtSlot 拉取；本格没变直接返回，空槽不为空才 ClearSlot），UnbindFromContainer 在换绑/NativeDestruct 时退订；显示数据是控件状态（CurrentItem/CurrentQuantity/bHasItemData），RefreshFromState() 是「状态 → 外观」唯一出口（NativeConstruct 也调它，构造/重建都能重放，别再手工 TakeWidget）；SetItemData 只写状态+重画（不要写数量早退，会连图标一起跳过）、ClearSlot 清状态+重画；NativeOnDragDetected 视觉类为空/指向原生类时退回 GetClass()、只 CreateWidget+InitializeSlot，创建 UInventorySlotDragDropOperation（回填 SourceContainer/SourceSlotIndex/SourceWidget）并半透明；NativeOnDrop 复位源控件显示 → 同槽/跨容器早退 → 发 Server_MoveOrMerge；DragOver 只接受本系统拖拽、Enter/Leave 高亮（SetHighlight_Implementation 染色） (~2800 tok)
- `InventorySlotDragDropOperation.cpp` — 拖拽操作实现：RestoreSourceWidget() 经弱引用 Pin 到源控件后 SetRenderOpacity(1.f)（失效即跳过） (~150 tok)
