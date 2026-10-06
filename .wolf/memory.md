# Memory

> Chronological action log. Hooks and AI append to this file automatically.
> Old sessions are consolidated by the daemon weekly.

| 21:04 | ItemContainer 增加 ItemTable + FindItemByID/AddItemByID（按 ItemID 查表、按堆叠规则放入） | Source/Wildforge/Public/ItemSystem/Components/ItemContainer.h, Private/.../ItemContainer.cpp | 完成，UHT 通过 | ~6k |
| 21:04 | APlayerCharacter 补齐所有服务器权威函数的 Server RPC（AddItem/RemoveAll/Organize/Clear/Resize/Initialize） | Source/Wildforge/Public/Character/Player/PlayerCharacter.h, Private/.../PlayerCharacter.cpp | 完成，UHT 通过 | ~3k |
| 21:04 | 尝试构建 WildforgeEditor | - | 被 Live Coding 阻塞（编辑器 PID 34068 在运行） | ~1k |
| 21:16 | 新增 UItemDatabaseSubsystem（持有 DT_Items + int 键懒缓存），ItemContainer 去掉 ItemTable/FindItemByID，改 AddItemStack | Public/Private/ItemSystem/Database/ItemDatabaseSubsystem.h/.cpp, ItemContainer.h/.cpp | 完成 | ~9k |
| 21:16 | Server RPC 从 APlayerCharacter 下沉到 UItemContainer 组件（所有拥有者自动继承） | ItemContainer.h/.cpp, PlayerCharacter.h/.cpp | 完成 | ~5k |
| 21:16 | 构建 WildforgeEditor | - | 成功（UHT 8 files，9 actions，链接通过） | ~2k |
| 21:22 | 收紧 RPC：移除 Server_InitializeContainer/Server_ResizeContainer，保留 Server_ClearContainer 供客户端 | ItemContainer.h/.cpp | 构建成功 | ~3k |
| 21:26 | DataTable 加载改为配置驱动（UCLASS(Config=Game) + UPROPERTY(Config) FSoftObjectPath），默认值写入 DefaultGame.ini | ItemDatabaseSubsystem.h/.cpp, Config/DefaultGame.ini | 构建成功 | ~4k |
| 21:36 | 配置改由 UDeveloperSettings 承载：新增 UItemSystemSettings（Project Settings 可编辑），子系统改读 GetDefault<>；ini 段改名为 ItemSystemSettings；Build.cs 显式加 DeveloperSettings 依赖 | ItemSystemSettings.h/.cpp(新), ItemDatabaseSubsystem.h/.cpp, Config/DefaultGame.ini, Wildforge.Build.cs | 构建成功（8 actions） | ~5k |
| 21:58 | 新增日志工具 UWildforgeLog（UE_LOG 到终端 + 按日期/等级写 Saved/Logs/Wildforge/<日期>/<等级>.log，临界区串行化，WFLOG_* 宏） | Public/Private/Core/Logging/WildforgeLog.h/.cpp(新) | 构建成功（修复 UHT 默认参数错误后） | ~6k |
| 22:15 | 日志改为异步：FWildforgeLogDevice(FOutputDevice) 挂 GLog，按等级分派 3 个 FLevelFileWriter（各自 FAsyncWriter 后台线程 + 独立锁，队列+异步）；模块改 FWildforgeModule 在 Startup/Shutdown 注册 | WildforgeLog.h/.cpp, Wildforge.cpp(模块类) | 构建成功（修复 FName 比较、Super 两处编译错误后） | ~8k |
| 22:23 | 日志文件从 Core/Logging 迁到新建的 Utils/（与 Core 同级），更新 include；ItemContainer 恢复 AddItemByID(ItemID,Quantity)（内部经 UItemDatabaseSubsystem 查表 + AddItemStack），Server_AddItem 复用它 | Public/Private/Utils/WildforgeLog.h/.cpp(移动), Wildforge.cpp, ItemContainer.h/.cpp | 构建成功（7 actions） | ~4k |
| 22:50 | RemoveItem/RemoveAllItem 增加 Quantity 参数（默认 -1 = 全部），新增私有 RemoveItemQuantityInternal 按堆叠逐格扣除（部分扣除保留槽位、扣空释放）；Server_RemoveItem/Server_RemoveAllItem 同步加 Quantity 并放宽 _Validate（负数=全部，正数上限 10000） | Public/.../ItemContainer.h, Private/.../ItemContainer.cpp | 构建成功（5 actions，UHT 通过 RPC 默认参数 -1） | ~5k |
| 22:52 | 移除 API 按寻址方式拆分：RemoveItem 改按槽位(SlotIndex, Qty=-1)、RemoveAllItem 按 ItemID(ItemID, Qty=-1)；私有原语拆为 RemoveSlotQuantityInternal/RemoveByItemIDInternal；删除被覆盖的 RemoveItemAtSlot 与 Server_RemoveItemAtSlot，Server_RemoveItem 改收 SlotIndex | Public/.../ItemContainer.h, Private/.../ItemContainer.cpp | 构建成功（5 actions）；破坏性变更：RemoveItem 首参 ItemID→SlotIndex，蓝图需重连 | ~4k |
| 10:38 | ItemContainer 全部修改函数加 check(IsInGameThread())（开发期线程断言，Shipping 下裁掉）；OnRep_Slots 也加 | Private/.../ItemContainer.cpp | 构建成功 | ~3k |
| 10:38 | 修复 OpenWolf hooks 在 Windows 报 MODULE_NOT_FOUND：cmd 不展开 $CLAUDE_PROJECT_DIR，settings.json 6 个 hook 命令改为相对路径 node ".wolf/hooks/*.js"（以项目根为 CWD） | .claude/settings.json | node --check 全部通过 | ~2k |
| 10:38 | 修复用户 WIP UI 编译错误：删 InventorySlotWidget 不存在的 ItemInfo 赋值；Cast 类型名 UItemDragDropOperation→UInventorySlotDragDropOperation | Private/.../UI/InventorySlotWidget.cpp | 构建成功（4 actions） | ~3k |
| 10:44 | 补全 anatomy.md：新增 ItemSystem/UI 的 Public/Private 两节（Grid/InventoryUserWidget/InventorySlotWidget/DragDropOperation）；核对 tidy-coalescing-adleman 计划（OnContainerChanged→RefreshGrid）已实现 | .wolf/anatomy.md | 完成 | ~2k |

## Session: 2026-10-06 10:45

| Time | Action | File(s) | Outcome | ~Tokens |
|------|--------|---------|---------|--------|
| 11:20 | `UItemContainer::MoveOrMergeItem(FromSlot,ToSlot)`：目标空槽/不同 ID/不可堆叠/目标满堆 → SwapSlots；同 ID 可堆叠且未满 → 先释放源槽再 AddItemStack，溢出留源槽 | Public/.../Components/ItemContainer.h, Private/.../ItemContainer.cpp | 完成（未编译验证） | ~5k |
| 11:20 | 新增 RPC `Server_MoveOrMerge(FromSlot,ToSlot)`（_Validate 只做非负检查），UI 不再直接调 BlueprintAuthorityOnly 的 SwapSlots | Public/.../Components/ItemContainer.h, Private/.../ItemContainer.cpp | 完成（未编译验证） | ~2k |
| 11:25 | `UInventorySlotDragDropOperation` 新增 `TWeakObjectPtr<UUserWidget> SourceWidget` + `RestoreSourceWidget()`，回答悬空指针/竞态质疑 | Public/.../UI/InventorySlotDragDropOperation.h, Private/.../UI/InventorySlotDragDropOperation.cpp | 完成（未编译验证） | ~3k |
| 11:30 | `NativeOnDrop` 补全：先复位源控件显示 → 同槽/跨容器早退 → 发 Server_MoveOrMerge；`NativeOnDragOver` 只接受本系统拖拽；`NativeDestruct` 兜底复位；DragDetected 回填 SourceSlotIndex/SourceWidget | Private/.../UI/InventorySlotWidget.cpp, Public/.../UI/InventorySlotWidget.h | 完成（未编译验证） | ~4k |
| 11:40 | 引擎源码取证：UWidget::SetRenderOpacity 对已销毁控件是空操作（Widget.cpp:454-463 + GetCachedWidget 弱指针 1116-1124）；更新 cerebrum（Key Learnings/Do-Not-Repeat/Decision Log） | .wolf/cerebrum.md | 完成 | ~6k |
| 11:45 | 记录 bug-007：UI 直接调用 BlueprintAuthorityOnly 的容器修改函数（客户端静默无效） | .wolf/buglog.json | 完成 | ~1k |
| 11:45 | 本会话 pwsh 工具全程不可用（SetNamedSecurityInfoW failed Win32 5），无法编译验证 | - | 受限，已告知用户 | ~1k |
| 12:10 | 用户质疑：为何直接调 Server_MoveOrMerge 而非 MoveOrMergeItem（答：BlueprintAuthorityOnly 不转发 RPC）；为何先 Remove 再 AddItemStack（采纳质疑）→ MoveOrMergeItem 合并改为直接在源/目标两槽间重分配 ItemQuality、源清空才回收槽位，一次广播 | Private/.../ItemContainer.cpp, Public/.../ItemContainer.h | 完成（未编译验证） | ~4k |
| 12:15 | 引擎取证：FRepLayout::CompareProperties 按元素与 shadow state 比较，原地改 Slots[i].ItemQuality 可正常复制（AddItemStack 已依赖此行为）；更新 cerebrum（Key Learnings + 两问的机制说明） | .wolf/cerebrum.md, .wolf/anatomy.md | 完成 | ~5k |
| 12:20 | 事故与修复：写 .wolf/anatomy.md 时误截断为源码部分，随后用会话内读到的内容重建全文（Content 素材条目为凭记忆还原，需重扫覆盖） | .wolf/anatomy.md | 已重建，待 openwolf 重扫核对 | ~3k |
