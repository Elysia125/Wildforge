# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Last updated: 2026-10-06

**本文件的写法（约定）**

- 只记录**跨会话仍然成立**的结论：用户偏好 / 项目约定与机制 / 禁令 / 架构决策。
- 每个条目**一两行**讲清结论；一次 bug 的完整因果写在 `.wolf/buglog.json`，
  这里只写"提炼后的禁令 + `见 bug-NNN`"，**不要在两处各写一遍因果**。
- **重要的强制约束不要写在这里**（本文件靠"AI 主动去读"才生效）——写
  `CLAUDE.md`（跨成员、入库）或 `CLAUDE.LOCAL.md`（本机专属、不入 git），
  它们会被 harness 每轮自动注入。完整的分流约定见 `CLAUDE.md` 的「上下文文件分工」。
- ⚠️ 下面的 `## Do-Not-Repeat` 标题被 `.wolf/hooks/pre-write.js` 硬编码用于提取告警模式，
  **不可改名、不可搬家**；格式相关的改动前先看 hook 实现。
## User Preferences

<!-- How the user likes things done. Code style, tools, patterns, communication. -->

- [2026-10-05] **倾向「一次给最优解」**：不要用渐进式重构（先硬编码→再 Config→再
  UDeveloperSettings）分多轮交付。先想清楚标准方案一次落地；若确有多种方案，先简短
  说明权衡，再直接实现最优的那个。
- [2026-10-05] 用户会追问机制原理（如「配置是怎么注入的」），解释要讲到原理层面，不要只给结论。
- [2026-10-06] 用户会主动质疑**并发 / 生命周期 / 悬空指针**层面的问题。回答时分三层
  （会不会崩 / 会不会丢状态 / 有没有更稳的做法），并**引用引擎源码证据**（文件:行号），
  不要只给经验结论。
- [2026-10-06] **本机专属的命令与路径放 `CLAUDE.LOCAL.md`**（不入 git），跨成员通用的强制
  约束放 `CLAUDE.md`（两者都会被 harness 每轮自动注入）。不要把这类内容堆到 `.wolf/` 里。
- [2026-10-06] **`.wolf/` 里的"harness 裁剪说明"这类自述文档，用户认为没必要、会自己删掉。**
  需要留存的只有**靠读代码发现不了、且会静默失效的机械依赖**（例如标题被 hook 硬编码），
  这类事实写在**被它约束的那个文件旁边**（如 cerebrum.md 的 Do-Not-Repeat 注释），
  不要单独开一节写"我们改过什么"。
- [2026-10-06] 用户会删除他判断为冗余的文档段落。**发现自己的补充被删掉时不要自动加回**——
  先确认是有意删除，是多此一举就撤回。
- [2026-10-06] **"信息该写到哪个文件"的分流约定已成文**（用户要求）：完整因果 → `buglog.json`；
  提炼后的禁令（一两行 + `见 bug-NNN`）→ 本文件 `## Do-Not-Repeat`；跨会话机制/约定 →
  `## Key Learnings`；架构取舍 → `## Decision Log`；会话流水 → `memory.md`；
  **强制约束 → `CLAUDE.md` / `CLAUDE.LOCAL.md`**。表格见 `CLAUDE.md` 的「上下文文件分工」。

## Key Learnings

- **Project:** Wildforge（UE 5.7，服务器权威 + 网络复制；单模块 `Wildforge`）
- 物品数据表在 `/Game/ItemSystem/DataTables/DT_Items`，由 `UItemDatabaseSubsystem` 从
  `UItemSystemSettings`（Project Settings > Game > Item System）`LoadSynchronous` 加载，
  懒建 `TMap<int32, FItemInformation>` 缓存；键优先取行内 `ItemID`，为 0 时回退行名数字部分。
- **物品「数量」统一沿用 `FItemInformation::ItemQuality`**（结构体没有独立数量字段），
  容器与 UI 全链路都这么用。
- **`UItemContainer` 两个添加 API 的分工**：`AddItemStack(定义, 数量)` 是纯存储原语
  （调用方需自备完整 `FItemInformation`）；`AddItemByID(ItemID, 数量)` 是服务器权威便利
  入口，内部经 `UItemDatabaseSubsystem` 查表再调 `AddItemStack`（`Server_AddItem` 转调它）。
- **移除 API 按寻址方式分家**：`RemoveItem(SlotIndex, Qty=-1)` 按槽位；
  `RemoveAllItem(ItemID, Qty=-1)` 按 ItemID 跨槽从后往前扣；`Qty < 0` 一律表示「全部」。
  私有无广播原语：`RemoveSlotQuantityInternal`（单槽）/ `RemoveByItemIDInternal`（跨槽）。
- **客户端背包 RPC 只传 `ItemID` + 数量或槽位号**，绝不接受客户端传来的 `FItemInformation`
  （防伪造）；真正的越界/占用检查在容器内部做。UHT **允许** RPC 使用默认参数（如 `Qty = -1`）。
- **`UItemContainer` 不加锁**：所有修改函数是 authority-only 且只在游戏线程执行，用
  `check(IsInGameThread())` 做开发期线程断言，不要引入互斥量。
- **日志体系**：`UWildforgeLog` 经 `LogWildforge` 同时输出终端与
  `Saved/Logs/Wildforge/<日期>/<等级>.log`；文件写入用引擎自带 `FAsyncWriter`（环形缓冲 +
  后台线程），调用线程只做一次内存拷贝。**C++ 一律用 `WFLOG_*` 宏、禁止 `UE_LOG`**
  （详见 `CLAUDE.md` 强制约束 3），唯一例外是 `WildforgeLog.cpp` 内部。
  `FOutputDevice::Serialize(V, Verbosity, Category)` 的 `V` 是裸消息，格式装饰由各设备自加。
- **构建**：编辑器运行时 Live Coding 会阻塞命令行构建（`Unable to build while Live Coding is
  active`）→ 用编辑器内 `Ctrl+Alt+F11`，或关掉编辑器再跑。被阻塞时 **UHT 仍会先跑一遍**，
  可用来提前发现反射（`UFUNCTION`/`UPROPERTY`）错误。
- **`BlueprintAuthorityOnly` 不具备任何自动 RPC 转发能力**：它只做「非权威端拒绝执行」
  （callspace = `Absorbed`，`Actor.cpp:5432`）。客户端要改服务端状态必须显式调 `Server_*` RPC。
  **Server RPC 在服务器上会就地同步执行**（`Actor.cpp:5522-5532` + `ScriptCore.cpp:1112-1149`），
  所以单机与联机走同一条路径——不要写 `HasAuthority() ? 本地调用 : 发 RPC` 的分支。
- **`HasAuthority()` 是 `AActor` 的方法，`UActorComponent` 上不存在**（写了报
  `error C3861`）。组件里判断权威端要用 `Utils/WildforgeAuthority.h` 的
  `IsAuthoritativeForActorComponent(this)`（即 `GetOwner()->HasAuthority()`，含空指针保护）。
- **跨容器拖拽（箱子等）需要额外的服务器校验通道**：接收端 `ShouldCallRemoteFunction`
  （`NetDriver.cpp:8242-8245`，`RepFlags.bNetOwner`）只接受「该 Actor 由该客户端
  PlayerController 拥有」的 Server RPC，服务器生成的箱子不满足——**包能发出，但在服务端被丢弃**；
  且服务器必须校验玩法权限（距离 / 交互状态）。正确做法是把转移 RPC 挂在**玩家自己**身上。
- **`UWidget::SetRenderOpacity` 对已销毁的控件是安全空操作**（`Widget.cpp:454-463` +
  `GetCachedWidget()` 用的是弱指针，`1116-1124`）。真正的风险是**状态丢失**（源格永久半透明）
  与裸指针地址被复用后**误改新控件**，所以拖拽记源控件要用 `TWeakObjectPtr`。
- **复制数组的「原地单元素修改」会被正常复制**：服务器变更检测走
  `FRepLayout::CompareProperties*`（按元素与 shadow state 比较），不要求整体重新赋值。
  `AddItemStack` 只改 `Slots[i].ItemQuality` 也能同步，就是依赖这个行为。
- **UMG 的构造时机分两段**：`CreateWidget` 只做 `Initialize()`（建 WidgetTree / 绑定 BindWidget
  属性 + `NativeOnInitialized`）；`NativePreConstruct` / `NativeConstruct` 要等 `TakeWidget()` →
  `RebuildWidget()` → `OnWidgetRebuilt()` 才跑（`UserWidget.cpp:1203`、`1208-1224`）。没有
  RootWidget 的 `UUserWidget` 只会渲染成 `SSpacer`。
- **UMG 没有「只构造一次」这回事**：`UWidget::MyWidget` / `MyGCWidget` 都是 `TWeakPtr`
  （`Widget.h:1187`、`1193`），Slate 树只由父级 slot 持强引用。谁丢掉了 `TakeWidget()` 的返回值
  （未挂进任何父控件的实例，例如拖拽视觉），整棵树立刻析构（`SObjectWidget::~SObjectWidget` →
  `ResetWidget` → `NativeDestruct` + `ReleaseSlateResources`，`SObjectWidget.cpp:42-86`），下次
  `TakeWidget()` 会重建并**再跑一次 `NativeConstruct`**。所以控件外观必须能从自己的状态重放
  （本项目 `UInventorySlotWidget::RefreshFromState()`），不要依赖「构造 / 灌数据」的先后顺序。见 bug-013。
- **`BlueprintImplementableEvent` → `BlueprintNativeEvent` 向后兼容**：WBP 里已有的同名事件节点
  会继续覆盖 C++ 的 `_Implementation` 默认实现（所以改完要确认 WBP 没留空事件节点）。
  判断 WBP 有没有实现某事件：读 `.uasset` 字节做 ASCII 搜索，**同时搜一个必然存在的对照字符串**
  （如 BindWidget 名 `QuantityText`）来确认这个方法可用——本项目实测可行（`SetHighlight` = 0 命中）。
- **`AddItemStack` 不适合做「拖拽合并」**：它按物品定义把数量堆到**任意**同 ItemID 的槽位
  （可能先填第三个未满堆），且中间会让 `UsedCount` 先减后加。拖拽合并应直接在源/目标两槽间
  重分配数量（目标加满、余数留源），源清空再回收槽位——此时 `ItemIDToSlot`/`FreeSlots`/
  `UsedCount` 全不变，一次 `NotifyContainerChanged()` 即可。
- **`FOnContainerChanged` 是「无参」动态多播**（`ItemContainer.h:14`），客户端那边还是
  `OnRep_Slots` 聚合一次再广播（`ItemContainer.cpp:47-52`）——**「哪个槽变了」这个信息根本不存在**。
  所以按需刷新只能由订阅者自己 diff：`UScriptStruct::CompareScriptStruct`（`Class.h:2406`，
  运行时可用）按反射逐属性比，结构体加字段也不用维护比较代码。
- **委托订阅的生命周期有引擎兜底**：`ProcessMulticastDelegate` 广播前拷贝调用列表
  （`ScriptDelegates.h:924-926`）、广播后 `CompactInvocationList()` 清失效绑定（`:945`），
  调用前还有 `IsBound()` 检查（`:931`）。所以「忘了退订」不会永久泄漏也不会调到已销毁对象；
  但**广播中新订阅的订阅者收不到这一轮通知**（列表已拷贝）——新建控件必须当场自己拉一次。
- **本项目有两条编译验证路径**：① 关掉编辑器后跑 UBT 纯构建（完整编译+链接）；
  ② 编辑器开着（Live Coding 占用）时，用 `clang-cl @<文件>.cpp.obj.rsp -fsyntax-only` 单文件检查
  ——rsp 就是 clangd 用的那份（含全部 `/I` `/D`），要剔除 `/Fo`、`/c`、`/clang:-M*`，
  工作目录必须是引擎 `Source` 目录（里面的 `/I` 是相对路径）；**不能用 `cl.exe`**（rsp 是
  clang 形态，且 MSVC 读不了 clang 的 .pch）。命令细节见 `CLAUDE.LOCAL.md`。
- **改 UCLASS/USTRUCT 头文件后必须先跑 UHT 再编译**：`GENERATED_BODY()` 展开成
  `FID_<file>_<line>_GENERATED_BODY`（宏名带行号），行号变了而 UHT 没重跑，就会报
  `a type specifier is required for all declarations`——那是宏对不上，不是代码写错（见 bug-014）。

## Do-Not-Repeat

<!-- Mistakes made and corrected. Each entry prevents the same mistake recurring. -->
<!-- Format: [YYYY-MM-DD] Description of what went wrong and what to do instead. -->
<!-- ⚠️ `.wolf/hooks/pre-write.js` 按本标题（## Do-Not-Repeat）硬编码切分，不可改名或搬走。 -->
- [2026-10-05] **不要**给 `AddItem` 传空的 `FItemInformation`（ItemID=0、空名、无图标、
  ItemQuality=0），物品会显示为空。用 `AddItemByID(ItemID, 数量)`，或先用 `Get Data Table Row`
  取到完整定义再传。（注：`AddItem` 已删除，见 bug-001。）
- [2026-10-05] `UFUNCTION` 的默认参数**不要**写 `FString()`（构造函数调用），UHT 会报
  "C++ Default parameter not parsed"；改用 `TEXT("")` 等 UHT 认得的字面量。普通非反射
  C++ 函数不受此限。
- [2026-10-05] 自定义 `FOutputDevice` 里**不要**拿 `Category != LogWildforge` 过滤类别：
  `Category` 是 `FName`，`LogWildforge` 是 `FLogCategory` 对象，直接比较会命中一堆模板
  `operator==` 而歧义编译失败。应比较静态 `FName`。
- [2026-10-05] 继承 `FDefaultGameModuleImpl` 时**没有** `Super` typedef（非反射类），
  基类调用要写全名：`FDefaultGameModuleImpl::StartupModule()`。
- [2026-10-06] Windows 下 `.claude/settings.json` 的 hook 命令里**不要**用 `$CLAUDE_PROJECT_DIR`
  （cmd 不展开 `$VAR`，会导致 `MODULE_NOT_FOUND`），改用相对路径 `node ".wolf/hooks/x.js"`。
- [2026-10-06] **UI 里不要直接调 authority-only 的修改函数**（`SwapSlots` / `OrganizeContainer` /
  `RemoveItem` …），客户端上会被静默丢弃——单机看不出、联机直接失效。UI 一律走 `Server_*` RPC。
  已踩两次：bug-007（拖拽 `SwapSlots`）、bug-008（整理按钮 `OrganizeContainer`）。
  **加新 UI 交互时先问：这个函数是不是 authority-only？**
- [2026-10-06] 组件里**不要**写 `HasAuthority()`（`UActorComponent` 上没有该标识符，C3861），
  用 `IsAuthoritativeForActorComponent(this)`。见 bug-009。
- [2026-10-06] **不要在 `Content/` 上做文本处理**：`.uasset` 是二进制，ripgrep 默认直接跳过、
  不会报错——用必然存在的字符串做对照才能发现「0 命中」其实是无结果。要查蓝图引用请用
  编辑器的 Find References，或让用户自查。
- [2026-10-06] **UMG 里不要用原生 C++ 类当拖拽视觉**（如 `UInventorySlotWidget::StaticClass()`）：
  原生类没有 WidgetTree，`RebuildWidget` 只会返回 `SSpacer`，表现是「鼠标下什么都没有」而
  不是报错。用 `GetClass()`/WBP 类。见 bug-011。
- [2026-10-06] **不要靠「先 `TakeWidget()` 再灌数据」来防 `NativeConstruct` 覆盖**：没有父级持有
  Slate 引用的控件（拖拽视觉），手工 `TakeWidget()` 的返回值一丢整棵树就析构，引擎随后还会重建并
  **再跑一次 `NativeConstruct`**，顺序约定必然失效。正解是把数据存成控件状态、在 `NativeConstruct`
  里按状态重放（`RefreshFromState()`）。见 bug-011、bug-013。
- [2026-10-06] `SetItemData` 里**不要**写 `if (Quantity <= 1) return;` 这种早退：它会把图标/名称/
  耐久一起跳过，单件物品整格空白。数量折叠只该由 `SetQuantity` 处理。见 bug-012。
- [2026-10-06] 头文件里用到引擎类型就**自己 include**，别靠 SharedPCH（UBT 编得过、clangd 一直报红）：
  例 `FTableRowBase` 要 `Engine/DataTable.h`。见 bug-014。
- [2026-10-06] **改完 UCLASS/USTRUCT 头文件不要直接编译**：先生成一次 UHT，否则按行号命名的
  `FID_..._<line>_GENERATED_BODY` 宏对不上，会看到 `a type specifier is required for all declarations`。
- [2026-10-06] **别再给 UI 加「容器一变就整表刷新」**：Grid 只做结构（容量/布局），内容由每个槽位
  自己按 (容器, 索引) 拉取 + diff。整表刷新会把 N 个格子的 Slate 写入全做一遍。见 bug-013 之后的
  Decision Log「背包槽位改为自持来源」。

## Decision Log

- [2026-10-05] 物品定义集中在 `UItemDatabaseSubsystem`（单一真相源），`UItemContainer` 回归
  纯存储；添加走 `Server_AddItem(ItemID, 数量)`（客户端只发 ID+数量，防伪造属性）。
- [2026-10-05] 背包 `Server` RPC 从 `APlayerCharacter` 下沉到 `UItemContainer` 组件，
  消除「每个拥有容器的类都重声明一遍 RPC」的重复。
- [2026-10-05] 物品表位置改用 `UItemSystemSettings`（UDeveloperSettings）声明而非子系统自带的
  `UPROPERTY(Config)`：可进 Project Settings UI 编辑、有资源选择器，子系统只负责消费。
- [2026-10-05] 日志放 `Utils/`（与 `Core/` 同级）；文件写入选**引擎自带 `FAsyncWriter` +
  自定义 `FOutputDevice`**，不引入 spdlog/glog、不自建队列线程。
- [2026-10-05] 移除 API 按**寻址方式**分工（`RemoveItem` 按槽位 / `RemoveAllItem` 按 ItemID），
  并删除被覆盖的 `RemoveItemAtSlot` / `Server_RemoveItemAtSlot`。**破坏性变更**：
  `RemoveItem` 首参由 ItemID 改为 SlotIndex，蓝图需重连。
- [2026-10-06] 拖拽「放置」的合并/交换规则落在 `UItemContainer::MoveOrMergeItem(From, To)` +
  RPC `Server_MoveOrMerge`，不散在 Widget 的 `NativeOnDrop` 里：合并要碰 5 个内部状态，
  UI 无权重写这些不变式；Widget 只负责「判定落点合法 + 发请求 + 复位视觉」。
- [2026-10-06] `UInventorySlotDragDropOperation::SourceWidget` 用 `TWeakObjectPtr<UUserWidget>`
  而非裸指针：拖拽期间容器广播变更可能导致网格刷新、源控件被销毁，裸指针悬空且地址可能被
  复用；恢复显示以源控件自身（`NativeOnDrop`/`NativeOnDragCancelled`/`NativeDestruct`）为主，
  弱引用只作兜底。
- [2026-10-06] **本机专属命令/路径放 `CLAUDE.LOCAL.md`（不入 git，已加 `.gitignore`）**，
  `CLAUDE.md` 只留跨成员通用内容；两边都会被 harness 自动注入。本机编译验证命令以
  `CLAUDE.LOCAL.md` 为准（`-Target=WildforgeEditor Win64 Development` **不能加引号**、
  `-OutputDir` 前必须有空格）。
- [2026-10-06] 格子高亮 `SetHighlight` 从 `BlueprintImplementableEvent` 改成 `BlueprintNativeEvent`：
  C++ 给默认表现（`SlotStyle` 描边染色 + 可调 `HighlightColor`），蓝图仍可覆写。选中/高亮两个
  状态分开记录、刷新时合成，避免 `SetSelected` 与 `SetHighlight` 互相冲掉颜色；拖拽视觉类默认
  取 `GetClass()`，`DragItemWidgetClass` 只作可选覆写（见 bug-011）。
- [2026-10-06] **裁剪 OpenWolf harness**：`anatomy.md` 只索引源码与配置（`Content/` 等二进制
  资产从 `config.json` 的 `anatomy.exclude_patterns` 排除，并手工剪掉历史条目；增量更新由
  `post-write.js` 的 `updateAnatomy()` 读同一份配置，见 bug-010）；`reframe-frameworks.md`
  已清空停用（本项目是 UE+UMG，无 Web 框架可选）；`cerebrum.md` 只留跨会话结论、与
  `buglog.json` 交叉引用而不重复叙述。
- [2026-10-06] `UInventorySlotWidget` 的外观改为**状态驱动**：`CurrentItem` / `CurrentQuantity` /
  `bHasItemData` 是唯一真相源，`RefreshFromState()` 是「状态 → 外观」的唯一出口，`NativeConstruct`
  只重放不清空，拖拽视觉不再手工 `TakeWidget()`。原因：Slate 控件随时可能被释放重建、
  `NativeConstruct` 会重复执行，一次性顺序约定不可靠（见 bug-013）。
- [2026-10-06] **背包槽位改为「自持来源」**：`UInventorySlotWidget::InitializeSlot(容器, 索引)`
  自己拉数据 + 自己订阅容器变更 + 本地 diff（`IsSameAsCurrentData`），`UItemContainerGrid` 退化为
  只管结构（`EnsureSlotCount` 返回是否变化，只有数量变了才 `LayoutSlots`），删掉 `UpdateSlot` /
  `SetOwningContainer` / `SetSlotIndex`（蓝图侧无引用，可安全改签名）。选它的理由**是结构不是速度**：
  数据源唯一（`GetItemAtSlot` O(1) 纯读）、槽位自洽可脱离 Grid 复用；省下来的只是视觉写入，
  订阅本身因为是 dynamic 多播反而更贵。顺带把 `OwningContainer` 从裸指针改成
  `UPROPERTY(Transient) TObjectPtr`（GC 可见、不会悬空），并在 `NativeDestruct` / 删槽位时显式退订。
- [2026-10-06] **槽位的对外 API 只留「来源」与状态标志**：外部只能 `InitializeSlot(容器, 索引)`
  （+ `UnbindFromContainer`/`RefreshFromContainer` 查询/手动刷新）、`SetSelected`/`SetHighlight`
  （控件自身的视觉标志）和 `SetSlotStyle`（主题皮肤，重建安全）。`SetItemData`/`ClearSlot` 及
  `SetQuantity`/`SetTopText`/`SetItemIcon`/`SetItemHP`/`SetItemStyle` 全部收成 private：它们只在
  「状态 -> 外观」里用，外部直接戳会在下一次容器广播 / Slate 重建时被状态覆盖。
  **数量只有一个来源：`FItemInformation::ItemQuality`**（不再有 `CurrentQuantity`，也没了
  `SetItemData` 的第二个参数；蓝图钩子 `OnItemDataSet` 也只收结构体）。
