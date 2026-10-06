# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Do not edit manually unless correcting an error.
> Last updated: 2026-10-06

## User Preferences

<!-- How the user likes things done. Code style, tools, patterns, communication. -->

- [2026-10-05] **倾向「一次给最优解」**：用户明确表示「建议下次直接给最优解而不是多次修改」。
  不要用渐进式重构（先硬编码→再 Config→再 UDeveloperSettings）分多轮交付；先想清楚
  标准/最佳实践方案，一次落地。若确实存在多种方案，先简短说明权衡再直接实现最优的那个。
- 用户会追问机制原理（如「配置是怎么注入的」），说明解释要讲到原理层面，不要只给结论。
- [2026-10-06] 用户会主动提出**并发/生命周期/悬空指针**层面的质疑（如「传源 Widget 指针
  会不会有竞态」）。回答这类问题要：先拆成「会不会崩 / 会不会丢状态 / 有没有更稳的做法」
  三层，并**引用引擎源码证据**（文件:行号），不要只给经验结论。

## Key Learnings

- **Project:** Wildforge
- **Description:** Developed with Unreal Engine 5
- 物品数据表路径为 `/Game/ItemSystem/DataTables/DT_Items`，由
  `UItemDatabaseSubsystem` 在 `Initialize()` 从项目设置
  （`GetDefault<UItemSystemSettings>()->ItemTable`）`LoadSynchronous` 加载。新增物品定义
  加到该数据表即可。
- 查表键约定：优先用行内 `ItemID` 字段，为 0 时回退取行名的数字部分。
- 物品「数量」统一沿用 `FItemInformation::ItemQuality`（结构体没有独立数量字段），
  因此 `AddItemStack` 写入的是 `ItemQuality = 该堆数量`。
- `UItemContainer` 的两种添加 API：`AddItemStack(定义, 数量)` 是纯存储原语（调用方需自备
  完整 `FItemInformation`）；`AddItemByID(ItemID, 数量)` 是服务器权威便利入口，内部走
  `UItemDatabaseSubsystem::GetItemDefinition` 查表再调 `AddItemStack`。
  `Server_AddItem` 的实现就是转调 `AddItemByID`。**UItemDatabaseSubsystem 的实际消费点**
  即在此（以及蓝图 `FindItem`/`GetItemDefinition`）。
- `UItemContainer` 的移除 API 按**寻址方式**分成两个，且都带数量：
  - `RemoveItem(SlotIndex, Quantity = -1)`：按**槽位**移除，从该槽位扣 Quantity 个，
    -1 表示清空整槽。
  - `RemoveAllItem(ItemID, Quantity = -1)`：按 **ItemID** 移除，跨该 ItemID 的所有槽位
    累计扣 Quantity 个（从最后一个槽位往前），-1 表示全部。
  `Quantity < 0` 一律表示「全部」（对应各自的寻址范围）；部分扣除保留槽位、扣空释放，
  整批只广播一次。私有无广播原语：`RemoveSlotQuantityInternal(SlotIndex, Qty)`（单槽）与
  `RemoveByItemIDInternal(ItemID, Qty)`（跨槽）。RPC `Server_RemoveItem(SlotIndex, Qty=-1)` /
  `Server_RemoveAllItem(ItemID, Qty=-1)` 与之一一对应。
  （原先按物移除的 `RemoveItemAtSlot` / `Server_RemoveItemAtSlot` 已删除——被
  `RemoveItem(SlotIndex,-1)` 覆盖。）
- UHT **允许** RPC（`Server, Reliable, WithValidation`）使用默认参数（如 `int32 Quantity = -1`），
  与普通 `UFUNCTION` 一样；蓝图节点会多出一个可选引脚，旧调用自动填默认值，无需改蓝图。
- 客户端背包 RPC 只传 `ItemID` + 数量，服务端按 ItemID 查表取定义，绝不接受客户端
  传来的 `FItemInformation`（防伪造）。
- 命令行 `Build.bat` 在编辑器（UnrealEditor.exe）运行时会被 Live Coding 阻塞：
  报 "Unable to build while Live Coding is active"。需在编辑器按 `Ctrl+Alt+F11`
  触发热编译，或关闭编辑器后再用命令行构建。注意此时 UHT 仍会先跑一遍（可用来
  提前发现反射错误）。
- 物品定义改由 `UItemDatabaseSubsystem`（GameInstanceSubsystem）集中持有：
  它加载 `DT_Items` 并懒构建 `TMap<int32, FItemInformation>` 缓存（按行内 ItemID，
  为 0 时回退行名数字部分）。`UItemContainer` 不再持有数据表，保持纯存储职责；
  `AddItemStack(FItemInformation, Quantity)` 只负责堆叠放置。
- `Server` RPC 声明在 `UItemContainer`（已复制的组件）上，所有拥有容器的类自动继承。
  **限制**：客户端只能对自己*拥有*的容器调 Server RPC；共享容器（箱子）需另走
  玩家身上的交互 RPC + 服务器校验（客户端无法对非自己拥有对象的 Server RPC 发出去）。
- 客户端可调的 RPC：Add / RemoveItem / RemoveItemAtSlot / RemoveAllItem / SwapSlots /
  MoveOrMerge / Organize / Clear。`InitializeContainer` / `ResizeContainer` **故意不做 RPC**，
  只保留 `BlueprintAuthorityOnly`，由服务器/游戏流程调用（避免客户端随意改容量/重置）。
- 物品数据表改为**项目设置驱动**：`UItemSystemSettings : UDeveloperSettings`
  （`UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Item System"))`）持有
  `UPROPERTY(Config, EditAnywhere) TSoftObjectPtr<UDataTable> ItemTable`，出现在
  Project Settings > Game > Item System，序列化到
  `Config/DefaultGame.ini` 的 `[/Script/Wildforge.ItemSystemSettings]`。
  `UItemDatabaseSubsystem::Initialize` 用 `GetDefault<UItemSystemSettings>()->ItemTable.LoadSynchronous()`
  取表。改表位置在编辑器里点选即可，不用改代码。
- `UDeveloperSettings` 属 `DeveloperSettings` 模块；虽被 `Engine` 公开依赖可传递获得，
  但凡公开头文件直接 include `Engine/DeveloperSettings.h`，`Build.cs` 应显式加
  `DeveloperSettings` 到 `PublicDependencyModuleNames`（本工程已加）。
- 日志统一走 `UWildforgeLog`（`Utils/WildforgeLog.h`，与 `Core/` 同级）：`Info/Warning/Error`
  经 `UE_LOG(LogWildforge, ...)` 输出到终端；文件由 `FWildforgeLogDevice`
  （`FOutputDevice`，挂在 `GLog` 上、模块 Startup/Shutdown 注册）异步落盘到
  `Saved/Logs/Wildforge/<YYYY-MM-DD>/<Info|Warning|Error>.log`。C++ 用
  `WFLOG_INFO/WARNING/ERROR("fmt", ...)` 宏，蓝图调同名静态函数。日志类别 `LogWildforge`。
- 日志文件写入用**引擎自带的 `FAsyncWriter`**（环形缓冲 + 后台线程，声明在
  `Misc/OutputDeviceFile.h`；引擎自己的 `Saved/Logs/*.log` 也用它）。每个等级一个
  `FLevelFileWriter`（独立 `FCriticalSection` + 独立 `FAsyncWriter`），调用线程只做一次
  内存拷贝，磁盘 I/O 全在后台 —— 这就是「队列 + 异步」的原生实现，无需引入 spdlog 等三方库。
  写字节时用 `FTCHARToUTF8` 转 UTF-8（无 BOM），不要直接序列化 TCHAR（UE 主日志的
  UTF-8 BOM + UTF-16 字节是历史遗留的不一致）。
- `FOutputDevice::Serialize(V, Verbosity, Category)` 里的 `V` 是**裸消息**（不含时间/类别），
  格式装饰由各设备自己加；4 参重载默认转发到 3 参，所以只重写 3 参即可。
- 另：DataTable 不是蓝图，是 `.uasset`；能用蓝图做数据的是 `UDataAsset`。
- UHT 以 `-WarningsAsErrors` 运行：`UPROPERTY(Config)` 若带 `Category` 却没有
  `EditAnywhere/BlueprintReadWrite` 等暴露关键字，会报 Category 警告并当成错误。
- `ConstructorHelpers::FObjectFinder` 只能在**构造函数**里用；在 `Initialize()` 等
  非构造阶段用会触发断言。子系统初始化阶段加载资源要用 `LoadObject`。
- `UItemContainer` **不加锁**：所有修改函数是 `BlueprintAuthorityOnly`，只在游戏线程调用；
  复制回调 `OnRep_Slots` 同样只在游戏线程。派生索引（`FreeSlots`/`ItemIDToSlot`/`UsedCount`）
  是**共享**状态，按「每个槽位一把锁」加锁粒度错误且会引入死锁/漏锁。正确做法是
  `check(IsInGameThread())` 在开发期断言线程归属（Shipping 下被裁掉），而不是加互斥量。
  `IsInGameThread()` 声明在 `CoreGlobals.h`（经 `CoreMinimal.h` 传递），无需额外 include。
  「只在游戏线程」不等于「会卡」：容器 `bCanEverTick=false`、操作 O(1) 且事件驱动；
  引擎的渲染/RHI/任务图/音频/异步加载另有独立线程。
- **`UWidget::SetRenderOpacity` 对已销毁的控件是安全空操作**（引擎源码证据：
  `Runtime/UMG/Private/Components/Widget.cpp:454-463` 先写 `RenderOpacity` 成员，再
  `GetCachedWidget()`，取不到 Slate 控件就跳过；`GetCachedWidget()` 在
  `Widget.cpp:1116-1124` 用 `MyGCWidget.Pin()` / `MyWidget.Pin()` 两个**弱指针**，
  销毁后自然失效）。所以「控件没了还去 Set」不会崩，真正的风险是**状态丢失**
  （源格子永久停在拖拽时的半透明）与**悬空指针**（若用裸 `UPROPERTY` 指针，被销毁的
  旧控件地址可能被新控件复用，于是误改新控件）。
- **`AddItemStack` 不适合做「拖拽合并」**：它是「按物品定义把数量重新堆到任意同
  ItemID 的槽位」——会遍历 `ItemIDToSlot`，可能先填满第三个同 ID 的未满堆，而不是
  本次拖拽的目标槽；中间还会让 `UsedCount` 先减后加、把刚释放的源槽重新分配一次。
  拖拽合并的语义是「只有源/目标这两槽之间移动数量」，应在容器里直接重分配这两个
  槽位的数量（`Slots[To].ItemQuality = TargetCount + Move; Slots[From].ItemQuality
  = SourceCount - Move;`），源被清空时再 `RemoveItemAtSlotInternal` 回收槽位。
  此时 `ItemIDToSlot`/`FreeSlots`/`UsedCount` 三个派生索引全部不变，不需要
  `RebuildDerivedState`，最后 `NotifyContainerChanged()` 广播一次即可。
- **复制数组的「原地单元素修改」会被正常复制**：`Slots` 是 `ReplicatedUsing` 数组，
  服务器的变更检测走 `FRepLayout::CompareProperties*`（`RepLayout.cpp`）——它按元素
  逐属性与 shadow state 比较后才写 changelist，并不要求 `Slots` 整体重新赋值或
  `MarkItemDirty`。现有代码已经依赖这一点：`AddItemStack` 只改
  `Slots[SlotIndex].ItemQuality`（`ItemContainer.cpp:167`）后广播，客户端就能收到。
  所以「直接改两个槽位的数量」是安全且合法的同步方式；`NotifyContainerChanged()`
  只负责刷新本地 UI，与复制无关。
- **`BlueprintAuthorityOnly` 不具备任何自动 RPC 转发能力**：它只做「非权威端调用时
  拒绝执行」，不会把调用转成 RPC。客户端要改服务端状态，必须显式调用
  `UFUNCTION(Server, Reliable, WithValidation)` 的 `Server_*` 版本——这是引擎里唯一
  的自动转发通道。`Server` RPC 在**独占服务器/监听服务器上会就地同步执行**，所以
  单机与联机走同一条代码路径，不需要再写 `HasAuthority() ? 本地调用 : RPC` 的分支
  （那种分支反而会让两条路径行为有分歧）。
- **本会话 `pwsh` 工具不可用**：调用任何命令都返回
  `Error: SetNamedSecurityInfoW failed (Win32 5): grantWrite(E:\UE_Projects\Wildforge)`，
  连 `cmd /c echo ok` 也一样（疑似沙箱 ACL 修复失败，与命令内容无关）。此时无法
  编译/运行任何验证，只能靠 read/grep 逐行自检，并向用户说明「未编译验证」。
  修复办法见 skill `diagnose-windows-sandbox-acl`（未能执行）。

## Do-Not-Repeat

<!-- Mistakes made and corrected. Each entry prevents the same mistake recurring. -->
<!-- Format: [YYYY-MM-DD] Description of what went wrong and what to do instead. -->
- [2026-10-05] 用蓝图的 `AddItem` 传入空的 `FItemInformation`（ItemID=0、空名、无图标、
  ItemQuality=0）导致物品显示为空。应改用 `AddItemByID(ItemID, 数量)`，或先用
  `Get Data Table Row` 取到完整定义再传。
- [2026-10-05] `UFUNCTION` 的默认参数**不能**写 `FString()`（构造函数调用），UHT 报
  "C++ Default parameter not parsed"。应改用 `TEXT("")`（或 `FString()` 之外的、UHT 认得的
  字面量）。普通非反射 C++ 函数不受此限，可继续用 `FString()`。
- [2026-10-05] 自定义 `FOutputDevice` 里**不要**用 `Category != LogWildforge` 过滤类别：
  `Category` 是 `FName`，而 `LogWildforge` 是 `FLogCategory` 对象，直接比较会命中一堆模板
  `operator==(TLazyObjectPtr/TWeakFieldPtr...)` 产生歧义编译失败。应比较静态 `FName`：
  `static const FName N(TEXT("LogWildforge")); if (Category != N) return;`。
- [2026-10-05] 继承 `FDefaultGameModuleImpl` 写自定义模块类时，**没有** `Super` typedef
  （非反射类）。基类调用要写全名：`FDefaultGameModuleImpl::StartupModule()`。
- [2026-10-06] Windows 下 `.claude/settings.json` 的 hook 命令里**不要写 `$CLAUDE_PROJECT_DIR`**：
  hooks 走 `cmd.exe`，不展开 `$VAR`，node 会收到字面量路径导致 `MODULE_NOT_FOUND`
  （报错路径形如 `...\<项目根>\$CLAUDE_PROJECT_DIR\.wolf\hooks\stop.js`）。改用**相对路径**
  `node ".wolf/hooks/x.js"`——hooks 以项目根为 CWD，cmd/bash 下都可用。
- [2026-10-06] **UI 里不要直接调 `UItemContainer` 的修改函数**（`SwapSlots` / `AddItem` /
  `RemoveItem` 等）：它们全是 `BlueprintAuthorityOnly`，客户端调用是静默空操作——
  单机测试看不出问题，联机时拖拽完全无效。UI 一律走 `Server_*` RPC。
  （原 `InventorySlotWidget::NativeOnDrop` 就是直接 `OwningContainer->SwapSlots(...)` 的坏例子。）

## Decision Log

- [2026-10-05] 物品添加走 `Server_AddItem(ItemID, Quantity)`：客户端只发 ID+数量，
  服务端查库 + 堆叠。理由：安全（防伪造属性），蓝图中不必手填整条 `FItemInformation`。
- [2026-10-05] 引入 `UItemDatabaseSubsystem` 作为物品定义的单一真相源（持有数据表 +
  int 键缓存）。理由：数据表不应散落在每个容器上，集中后便于缓存、校验、约定统一。
- [2026-10-05] 把物品定义的取用与存储分离：`UItemContainer` 回归纯存储
  （`AddItemStack` 只做堆叠放置），查表交给子系统。理由：职责单一，容器不再耦合数据表。
- [2026-10-05] 背包 `Server` RPC 从 `APlayerCharacter` 下沉到 `UItemContainer` 组件。
  理由：消除「每个拥有容器的类都重声明一遍 RPC」的重复；组件已是复制的。
- [2026-10-05] 物品表位置改用 `UItemSystemSettings`（UDeveloperSettings）声明，而非
  子系统自带的 `UPROPERTY(Config)`。理由：可进 Project Settings UI 直观编辑、有资源
  选择器（TSoftObjectPtr + AllowedClasses）、配置归属清晰；子系统只负责消费，不兼做配置。
- [2026-10-05] 日志代码放 `Utils/`（与 `Core/` 同级），不放 `Core/Logging/`。理由：日志是
  通用工具而非核心子系统，用户偏好 Utils 目录归类。
- [2026-10-05] 重新引入 `UItemContainer::AddItemByID(ItemID, Qty)`（服务器权威便利入口）。
  理由：之前只有 `AddItemStack(定义,...)`，蓝图侧需「FindItem → AddItemStack」两步且易传空
  定义（正是 bug-001 的成因）；`AddItemByID` 一步到位、类型安全。接受容器对
  `UItemDatabaseSubsystem`（GameInstance 服务）的单向依赖；`AddItemStack` 仍保留为纯存储原语。
- [2026-10-05] 日志文件写入选**引擎自带 `FAsyncWriter` + 自定义 `FOutputDevice`**，
  而非引入 spdlog/glog 或自建队列线程。理由：引擎已内置高性能环形缓冲异步写（引擎主日志
  同款），挂到 `GLog` 后 `UE_LOG(LogWildforge,...)` 自动落盘、零侵入；三方库会多一份依赖
  且与 UE 日志系统重复。终端输出仍走引擎默认设备（同步，无法在不换 console device 的前提下
  异步，但通常不是瓶颈）。
- [2026-10-05] 移除 API 按**寻址方式**分工（用户明确要求）：`RemoveItem(SlotIndex, Qty=-1)`
  按槽位、`RemoveAllItem(ItemID, Qty=-1)` 按 ItemID，两者都支持数量、-1=全部。理由：先前
  两者语义重复（都是按 ItemID 扣数量，`RemoveAllItem` 只是 `RemoveItem` 的别名）；按寻址
  维度拆分后各有明确用途——UI 右键某格用槽位版，配方/消耗按物品类型用 ItemID 版。同时删除
  被覆盖的 `RemoveItemAtSlot` / `Server_RemoveItemAtSlot`，避免三份重叠 API。**破坏性变更**：
  `RemoveItem` / `Server_RemoveItem` 首参从 ItemID 改为 SlotIndex，蓝图需重新连线。
- [2026-10-06] 拖拽「放置」的合并/交换规则落在 `UItemContainer::MoveOrMergeItem(FromSlot, ToSlot)`
  + RPC `Server_MoveOrMerge`，而不是散在 Widget 的 `NativeOnDrop` 里。理由：合并要碰
  `Slots`/`SlotOccupied`/`FreeSlots`/`ItemIDToSlot`/`UsedCount` 五个内部状态，UI 无权重写
  这些不变式；且 UI 代码在客户端必须经 RPC 才能改动容器。Widget 只负责「判定落点合法 +
  发请求 + 复位视觉」。
- [2026-10-06] `UInventorySlotDragDropOperation::SourceWidget` 用
  `TWeakObjectPtr<UUserWidget>` 而非裸 `UInventorySlotWidget*`。理由：拖拽期间容器广播
  变更 → 网格刷新 → 源控件可能被 `RemoveFromParent`/销毁，裸指针悬空且地址可能被复用而
  误改新控件；弱引用自动失效（`Pin()` 返回 null 即跳过），且不会因强引用把已从层级里摘掉的
  控件硬留在拖拽操作里。恢复显示以源控件自身（`NativeOnDrop` / `NativeOnDragCancelled` /
  `NativeDestruct`）为主，`SourceWidget` 只作兜底。
