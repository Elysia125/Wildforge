# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Do not edit manually unless correcting an error.
> Last updated: 2026-10-05

## User Preferences

<!-- How the user likes things done. Code style, tools, patterns, communication. -->

- [2026-10-05] **倾向「一次给最优解」**：用户明确表示「建议下次直接给最优解而不是多次修改」。
  不要用渐进式重构（先硬编码→再 Config→再 UDeveloperSettings）分多轮交付；先想清楚
  标准/最佳实践方案，一次落地。若确实存在多种方案，先简短说明权衡再直接实现最优的那个。
- 用户会追问机制原理（如「配置是怎么注入的」），说明解释要讲到原理层面，不要只给结论。

## Key Learnings

- **Project:** Wildforge
- **Description:** Developed with Unreal Engine 5
- 物品数据表路径为 `/Game/ItemSystem/DataTables/DT_Items`，由
  `UItemDatabaseSubsystem` 在 `Initialize()` 用 `LoadObject` 加载。新增物品定义
  加到该数据表即可。
- 查表键约定：优先用行内 `ItemID` 字段，为 0 时回退取行名的数字部分。
- 物品「数量」统一沿用 `FItemInformation::ItemQuality`（结构体没有独立数量字段），
  因此 `AddItemStack` 写入的是 `ItemQuality = 该堆数量`。
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
- 客户端可调的 RPC：Add / RemoveItem / RemoveItemAtSlot / RemoveAllItem / SwapSlots
  / Organize / Clear。`InitializeContainer` / `ResizeContainer` **故意不做 RPC**，
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
- 另：DataTable 不是蓝图，是 `.uasset`；能用蓝图做数据的是 `UDataAsset`。
- UHT 以 `-WarningsAsErrors` 运行：`UPROPERTY(Config)` 若带 `Category` 却没有
  `EditAnywhere/BlueprintReadWrite` 等暴露关键字，会报 Category 警告并当成错误。
- `ConstructorHelpers::FObjectFinder` 只能在**构造函数**里用；在 `Initialize()` 等
  非构造阶段用会触发断言。子系统初始化阶段加载资源要用 `LoadObject`。

## Do-Not-Repeat

<!-- Mistakes made and corrected. Each entry prevents the same mistake recurring. -->
<!-- Format: [YYYY-MM-DD] Description of what went wrong and what to do instead. -->
- [2026-10-05] 用蓝图的 `AddItem` 传入空的 `FItemInformation`（ItemID=0、空名、无图标、
  ItemQuality=0）导致物品显示为空。应改用 `AddItemByID(ItemID, 数量)`，或先用
  `Get Data Table Row` 取到完整定义再传。

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
