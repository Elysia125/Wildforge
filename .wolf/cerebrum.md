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
- **`AddItemStack` 不适合做「拖拽合并」**：它按物品定义把数量堆到**任意**同 ItemID 的槽位
  （可能先填第三个未满堆），且中间会让 `UsedCount` 先减后加。拖拽合并应直接在源/目标两槽间
  重分配数量（目标加满、余数留源），源清空再回收槽位——此时 `ItemIDToSlot`/`FreeSlots`/
  `UsedCount` 全不变，一次 `NotifyContainerChanged()` 即可。

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
- [2026-10-06] **裁剪 OpenWolf harness**：`anatomy.md` 只索引源码与配置（`Content/` 等二进制
  资产从 `config.json` 的 `anatomy.exclude_patterns` 排除，并手工剪掉历史条目；增量更新由
  `post-write.js` 的 `updateAnatomy()` 读同一份配置，见 bug-010）；`reframe-frameworks.md`
  已清空停用（本项目是 UE+UMG，无 Web 框架可选）；`cerebrum.md` 只留跨会话结论、与
  `buglog.json` 交叉引用而不重复叙述。
