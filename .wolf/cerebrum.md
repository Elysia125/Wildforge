# Cerebrum

> OpenWolf's learning memory. Updated automatically as the AI learns from interactions.
> Last updated: 2026-10-07

**本文件的写法（约定）**

- 只记录**跨会话仍然成立**的结论：用户偏好 / 项目约定与机制 / 禁令 / 架构决策。
- 每个条目**一两行**讲清结论；一次 bug 的完整因果写在 `.wolf/buglog.json`，
  这里只写"提炼后的禁令 + `见 bug-NNN`"，**不要在两处各写一遍因果**。
- **重要的强制约束不要写在这里**（本文件靠"AI 主动去读"才生效）——写
  `CLAUDE.md`（跨成员、入库）或 `CLAUDE.LOCAL.md`（本机专属、不入 git），
  它们会被 harness 每轮自动注入。完整的分流约定见 `CLAUDE.md` 的「上下文文件分工」。
- ⚠️ 下面的 `- **本机构建报 "UbaSessionServer ... Low on memory(x/y). Kill threshold is z" 不是 UBA 故障**：x/y 是「系统提交量(commit charge) / 提交上限(物理内存+页面文件)」，不是物理内存占用；阈值 95% 来自 `UbaScheduler.h` 的 `memStartKillPercent`（z = 0.95×y）。排查先看各进程的 commit（PowerShell `PrivateMemorySize64`），本机常是 clangd 占用过高。

## Do-Not-Repeat` 标题被 `.wolf/hooks/pre-write.js` 硬编码用于提取告警模式，
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
- [2026-10-07] 本机内存紧张（物理 ~32GB / 提交上限 ~67GB）：**不要**把 clangd 配成 `--pch-storage=memory` + `-j=16`——单进程提交量会到 25GB+，让 UBA 内存看门狗在构建时杀编译进程（见 bug-020）。改用 `--pch-storage=disk` + 较小的 `-j`。
- [2026-10-07] 用户对自己设计的玩法约束很确定（如「趴下之后不能加速」）。他否定某个方向（「和 X 没关系」）时
  就接受、把 X 移出假设集，但**仍要用日志/源码核实**——同一次排查里日志反而证明了趴下状态确实能触发加速
  （`StartSpeedBoost` 基准=150→目标 1200，此时趴下=1）：**设计意图 ≠ 当前实现**，两者分开讲，别顺着意图改方向。见 bug-043。
- [2026-10-08] 用户对「配置」的要求是**三件事同时成立**：① 改完不用重新编译、也不用重新打包；
  ② 只能由服务器读；③ 值由服务器下发给客户端。设计任何配置类都按这个信任边界走——客户端本地
  的 ini 属于「玩家可改的文件」，绝不能当作玩法数值的来源。
- [2026-10-08] 选方案时用户直接选了**全量接入**（六个能力组件一次性接完）而不是先接一两个试点，
  与「一次给最优解」偏好一致：别提议「先小范围试」，除非确实有必须先验证的技术风险。
- [2026-10-08] **配置要按「归属」拆开，不许挤进一个类**：任何配置项都应归属到它真正服务的那个
  组件/系统，由**单独的类（或结构体）**管理——「不要把所有组件、所有地方的配置值全部都挤到一起，
  可读性高一点」。所以：一个组件的调参只出现在它自己的配置类里（本项目即
  `Character/Settings/<X>ComponentSettings.h`，一类一个 Project Settings 页 / 一个 ini 节），
  不要为了「集中管理」把它们塞进 `UWildforgeGameplaySettings` 这种大杂烩。落笔前先想清楚
  「这个值到底属于谁」；跨组件共用的值（如背包容量）放到**宿主**的配置类里，不要复制两份。

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
- **超大资源包不要进 git**：`Content/ProceduralNaturePack/`（1.8G、222 个 .uasset/.umap）已加进
  `.gitignore`，只留在本地磁盘、不入库。它当初只被添加在**未 push 的那一个 tip commit**里，
  所以移除历史用的是最轻的手段：`git rm -r --cached` 取消追踪（保留工作区文件）+
  `git commit --amend --no-edit` 重写该 commit——**不需要 `filter-repo`/`filter-branch`**。
  判定「要不要动真格的历史重写」先跑 `git log --all --oneline -- <路径>`：只在未推送的 tip
  出现就能靠 amend 解决，一旦出现在已推送历史里才需要 filter-repo 且必须协调远端。
- **git-lfs 的对象不会因为重写历史自动回收**：`.git/lfs/objects` 里未被任何 ref 引用的对象仍在
  （本项目重写后 `git lfs prune --dry-run` 显示可清 830 个文件 / 约 1.9G，含本次的资源包和
  早先重写遗留的孤儿）。`git lfs prune` 只删无引用对象、不动工作区文件，是安全的；但仍是**不可逆
  删除**，执行前先 `--dry-run`、必要时 `--verify-remote`。

- **引擎结构体里的浮点 `RoundTo*` 重载在 UE 5.7 已改名**：`FMath::RoundToFloat` 不存在（本机实测
  UnrealMathUtility.h 只剩 `RoundToZero` / `RoundToNegativeInfinity` / `RoundToPositiveInfinity`
  的 float+double 重载），要取整到整数高度就用 `static_cast<float>(FMath::RoundToDouble(X))`。
- **`UPARAM(ref)` 的输出引用不能排在「带默认值」的参数之后**（C++ 规则：默认值一旦开始，后面的参数都得有
  默认值），否则编译器报 `missing default argument on parameter 'X'`——**UHT 不检查这条**，报错点在参数名上，
  看起来像 UPARAM 的问题其实不是。正确做法是把输出引用放参数表**第一个**。见 bug-016。
- **只前置声明的引擎类型要自己 include**：`GameFramework/Character.h` 只写了 `class UCapsuleComponent;`，
  用 `GetCapsuleComponent()->GetScaledCapsuleRadius()` 必须 `#include "Components/CapsuleComponent.h"`
  ——UBT 靠 SharedPCH 编得过，本项目的只读语法检查路径会报 incomplete type。同 bug-014。
- **`FTimerManager::SetTimer` 的 `InRate <= 0` 是「清掉该句柄上的定时器」，不是「每帧触发」**：
  `TimerManager.h:157` 的文档与 `TimerManager.cpp:617-659` 的实现都是这样（else 分支只 `InOutHandle.Invalidate()`，
  根本不会 AddTimer）。唯一「每帧」的定时器 API 是 `SetTimerForNextTick`（`TimerManager.cpp:662-702`，内部
  Rate 固定 0、bLoop=false），需要回调里自己续挂。见 bug-017。
- **`GetTimerElapsed(Handle)` 是按 Rate 反推出来的**（`TimerManager.cpp:795-812` 的
  `Rate - (ExpireTime - InternalTime)`）：掉帧时 TimerManager::Tick 会一次补触发多次
  （`CallCount = (InternalTime - ExpireTime)/Rate + 1`，`:1057-1059`），此时它会给出负值。
  累计时长要用世界时间差（`World->GetTimeSeconds()` 差分），不要累加它。
- **Enhanced Input 的 `Completed` = 「Trigger 状态 Triggered→None」，不是「按键松开」**
  （定义 `InputTriggers.h:52-55`，判定 `EnhancedPlayerInput.cpp:119-122`）。Hold 触发器勾了 Is One Shot 会在
  达标后的下一帧返回 None（`InputTriggers.cpp:167-171`），Tap 触发器按住超过 `TapReleaseTimeThreshold` 也返回
  None（`:207-211`）——两者都会在手还按着时发 Completed。见 bug-018。
- **判定 `.uasset` 里某属性是否为非默认值**：按字节把文件读成 ASCII 看属性名在不在名字表里——只有被序列化的
  （非默认的）tagged property 才会把属性名写进名字表；对照组（IA_Jump 的 `ActuationThreshold` 是默认值、
  名字表里查不到）能确认这个方法有效。ripgrep 会静默跳过二进制 .uasset，见 Do-Not-Repeat。
- **引擎默认「不复制蒙太奇播放」**：`ACharacter` 只复制 RootMotion 那一段的状态（`FRepRootMotionMontage`），
  普通 `Montage_Play` 不会同步到客户端；引擎源码里搜不到 `RepAnimMontageInfo`（那是 GAS 的东西，本项目未启用
  GAS）。所以「只在服务器 Montage_Play」= 客户端看不到攻击动画，且**客户端的 OnMontageEnded 永不触发**，
  依赖它的清理逻辑（恢复移动、复位状态）在客户端全部失效。要在客户端播动画必须自己 `NetMulticast`。
- **`PerformDamageTrace` 这类攻击判定的解耦做法：不自己扣血，只发伤害**。扫掠（`SweepMultiByChannel`）拿到
  `TArray<FHitResult>` 后逐个 `UGameplayStatics::ApplyDamage`，攻击方只依赖 `AActor` 基类；受击方自己实现
  `TakeDamage` 或绑蓝图 `AnyDamage`，`CanBeDamaged() == false` 的目标静默跳过。**组件无需知道被打的是什么**
  （新增受击者类型不用改攻击代码）；`ApplyPointDamage`/GameplayEffect 是同一管线的升级形态。
- **复制的「反应式下标」不能在异步回调里反查当前物体**：服务器 `AttackMontageIndex++` 与客户端收到复制
  不保证同帧，`OnMontageEnded` 里用下标查表会错配成「下一段」而丢掉结束事件。正确做法是**复制「当前在播的
  对象」本身**（本项目 `ActiveAttackMontage`），让判定与下标推进解耦。
- **`UAnimMontage` 的长度在 UE 5.7 要用 `GetPlayLength()`**：`SequenceLength` 既是 `protected` 又已
  `UE_DEPRECATED`，直接读编译不过（见 bug-019）。
- **`BlueprintNativeEvent` 的 `_Implementation` 声明是 UHT 生成的，签名必须逐字对上**：它对
  枚举参数生成的是**裸枚举类型**（如 `ECollisionChannel`），不是 `TEnumAsByte<...>`——头文件里
  写 `TEnumAsByte` 会在编译期报
  `out-of-line definition of 'X_Implementation' does not match any declaration`
  （UHT 自己反而能过）。旧的 `UFUNCTION(BlueprintCallable)` 没有生成声明，隐式转换够用，
  所以这条只在改成 `BlueprintNativeEvent` 时才暴露。见 bug-023。
- **`TEnumAsByte<T>` 不要和裸枚举混进同一个三元表达式**：`cond ? MovementMode : MOVE_None`
  两个方向都能隐式转换，clang 报 `conditional expression is ambiguous`。统一取
  `.GetValue()` 或统一用裸枚举。同 bug-023。
- **`GetWorldTimerManager()` 是 `AActor` 的方法，组件上没有**（和 `HasAuthority()` 同一类坑）：
  组件里要写 `GetWorld()->GetTimerManager()`；而且 `Engine/World.h` 只前置声明 `FTimerManager`，
  用 `SetTimer`/`FTimerDelegate`/`ClearTimer` 必须自己 `#include "TimerManager.h"`。
  把代码从 Actor 搬进组件、或重写组件头文件时最容易丢这条。见 bug-023。
- **`COND_SimulatedOnly` 不会发给自主代理（本地玩家自己）**：它只发模拟端，而
  「本地玩家的 UI / 输入门控要读的状态」恰好只有自主代理需要。组件里要让本人读到的状态
  要用 `COND_OwnerOnly`（本项目 `ULandRollComponent` 的旧写法用的是 SimulatedOnly，
  新组件 `USprintBoostComponent` / `UBlinkComponent` 改用了 OwnerOnly）。
  另外组件的 `DOREPLIFETIME_*` 只有在构造函数里 `SetIsReplicatedByDefault(true)`
  且宿主 Actor `bReplicates = true` 时才真的生效。
- **移动输入的「方向」在服务器上要读 `Acceleration`，不能读 last input vector**：后者
  （`UPawnMovementComponent::GetLastInputVector()` / `APawn::GetLastMovementInputVector()`）
  只是 `APawn::LastControlInputVector` 的转发（`Pawn.cpp:819-822`），而它**只由客户端的
  `AddMovementInput` 累加**——服务器上恒为零向量，于是「服务器判方向」会静默失效
  （永远读到「没有输入」）。服务器能拿到的是客户端 move 包里的 `Acceleration`
  （`MoveAutonomous`：`Acceleration = ConstrainInputAcceleration(NewAccel)`，
  `CharacterMovementComponent.cpp:10512`；客户端则在 `ControlledCharacterMove` 的
  `:6350` 设置），两端都有效，读法 `UCharacterMovementComponent::GetCurrentAcceleration()`。
  两个坑：**它的长度不是输入强度**（`ScaleInputAcceleration` 在无输入但仍有速度时会把它
  填成 `Velocity.GetSafeNormal()`，长度恒为 1，见 `:3810-3820`，所以只能看方向不能看长度）；
  **模拟代理上它只在 `bRepAcceleration` 为真时才是真值**（否则退化为速度方向，`:6866-6881`），
  所以只在权威端拿它做判定。
- **`COND_OwnerOnly` 与 `COND_SimulatedOnly` 是两个互斥子集，选错就是「该看到的人看不到」**：引擎里就三行
  判定（`RepLayout.h:149-152`：`ConditionMap[COND_OwnerOnly] = bIsOwner`、
  `ConditionMap[COND_SimulatedOnly] = bIsSimulated`），而 `bNetOwner` 由「这条连接是不是角色的拥有者」决定
  （`DataChannel.cpp:3787`）、`bNetSimulated` 由「复制期间角色是不是被降级成 SimulatedProxy」决定
  （`:3812`，非拥有者的连接会在 `FScopedRoleDowngrade` 里被临时降级，`:3529-3541`）。于是：
  **OwnerOnly = 只有本地玩家自己收得到；SimulatedOnly = 只有别人收得到，本地玩家反而收不到**。
  想让**所有人**都读到，必须**不带 condition**（`DOREPLIFETIME` 裸写 = `COND_None`）。
  选择规则先问「这个值最终是谁消费的」：拥有者客户端的 UI → OwnerOnly；其他玩家的表现 → SimulatedOnly；
  两端都要 → 无 condition。（本项目 LandRoll 曾用 SimulatedOnly，于是本地玩家永远读不到自己的
  `bIsRolling`；见 bug-030。）
- **客户端上调用或接收 `NetMulticast` 都只会本地执行**：`AActor::GetFunctionCallspace` 对 Multicast 在服务器
  返回 `Local | Remote`、在客户端返回 `Local`（`Actor.cpp:5500-5519`），所以客户端本地调用它既不发给服务器、
  也不广播给别人；收到服务器发来的那一次同样是「非权威端在执行」。**推论：Multicast 的 `_Implementation`
  里绝不能加「非权威端就忽略」这类门禁**——那等于客户端永远看不到该表现（本项目翻滚特效就这样被整段挡掉，
  见 bug-030）。判据只能是「这个端要做什么」，不是「是谁调用的」。
- **`MOVE_None` 会丢弃根运动**：`UCharacterMovementComponent::PerformMovement` 入口对
  `MovementMode == MOVE_None` 直接 return，且显式 `RootMotionParams.Clear()` / `CurrentRootMotion.Clear()`
  再 `ClearAccumulatedForces()`（`CharacterMovementComponent.cpp:2716-2734`）。所以 `DisableMovement()` 是
  「连动画自带的位移一起关掉」，只适合不需要位移的动作（攻击）。需要位移的动作（翻滚 / 位移技）不要碰移动
  组件；`StopMovementImmediately()` 也会把跑动惯性清零，边跑边滚会变成急停 + 原地滚。见 bug-029。
- **`UCharacterMovementComponent` 在 UE 5.7 不复制任何移动参数**：头文件里 `MaxWalkSpeed` /
  `MaxWalkSpeedCrouched` / `GroundFriction` / `BrakingDecelerationWalking` / `MaxAcceleration`
  都**没有** `Replicated` 标记，cpp 里也**没有** `GetLifetimeReplicatedProps`（本机 grep 实测）。
  所以「服务器改 `MaxWalkSpeed`、客户端自动跟随」在本引擎**不成立**（UE4 时代那条
  `DOREPLIFETIME_CONDITION(..., MaxWalkSpeed, COND_SkipOwner)` 在 5.7 已不存在）——
  客户端仍按自己的值做本地预测（`GetMaxSpeed()` 直接读 `MaxWalkSpeed`，`CalcVelocity` 调用点
  `CharacterMovementComponent.cpp:3796`），与服务器的差异只能靠 `ClientAdjustPosition` 纠正。
  要改速度 / 摩擦这类参数又不想橡皮筋，必须在**自主代理上也写同一份**（`IsLocallyControlled()` 那一侧）。
  可行的最小做法（`USprintBoostComponent` 的实现）：把权威值放进自己的复制属性
  （`ReplicatedMaxWalkSpeed{,Crouched}`，`COND_OwnerOnly` 只发拥有者），拥有者在 `OnRep_*` 里
  立刻写进自己的移动组件、再用 Tick 兜底核对。它不会和服务器打架——服务器从不读客户端那份。
  见 bug-031。
- **客户端预测的容差小得反直觉：√3 ≈ 1.73 cm**。`AGameNetworkManager::ExceedsAllowablePositionError`
  判的是 `(LocDiff | LocDiff) > MAXPOSITIONERRORSQUARED`（`GameNetworkManager.cpp:166-169`），
  默认 `MAXPOSITIONERRORSQUARED = 3.0f`（同文件 :29）。客户端预测之所以敢用这么小的容差，是因为
  「同样的输入 + 同样的参数 = 逐帧可复现的同一个位置」；**一旦参数不同，速度差 × 时间就会瞬间越线**：
  500 cm/s 的差只要 3.5 ms。这条数字是「预测参数必须两端一致」的量化理由，也是排查
  「为什么一直有纠正」时该先算的那个数。
- **速度曲线（渐变类）的预测只能做到「同一条曲线、差一个 RTT/2」**：客户端拿到的是服务器
  RTT/2 之前的值，斜率 = (目标 − 起始) / 时长，默认 500/1.5 ≈ 333 cm/s² → RTT 50 ms 时约 8 cm/s
  的跟踪误差 = 每 ~0.2 s 一次 2 cm 级纠正。要零残差必须让服务器**按 move 包里的时间戳**重放
  （自定义 `UCharacterMovementComponent` 子类，在 `MoveAutonomous` 前按 `ClientTimeStamp` 设置速度）。
  本项目判定不值得（要换掉 `ACharacter` 的移动组件类，波及所有蓝图角色）。
  绝对不要为了「本地手感」让客户端自己推进曲线或自己提前结束——那只会把相位差放大成持续偏差。
- **不改 `MaxWalkSpeed` 也能做出高于步行上限的可控速度曲线**：`ApplyVelocityBraking` 在
  `Friction == 0 && BrakingDeceleration == 0` 时**直接 return**（`CharacterMovementComponent.cpp:4310-4316`），
  速度不会被移动组件吃掉；而 `CalcVelocity` 对**已经超速**的速度取
  `NewMaxInputSpeed = Velocity.Size()`（`:3863-3865`），所以 900 的滑行速度不会因为
  `MaxWalkSpeed == 600` 被夹回去。做法：`GroundFriction` / `BrakingDecelerationWalking` 置 0，
  每帧只把水平速度**往下夹**到自己的曲线值。转向强度由 `MaxAcceleration` 独家控制
  （`ScaleInputAcceleration` = `GetMaxAcceleration() * InputAcceleration`，`:8018`；置 0 = 输入完全不产生加速度）。
- **「本端进度」不能用复制的服务器时间戳减本端世界时间**：两端 `World->GetTimeSeconds()` 起点不同
  （客户端从自己的关卡加载算起），`本端 Now - 服务器时间戳` 是垃圾值。表现同步（Multicast / 复制）到达时
  **各端自己记一份本端起算时间**再算进度，UI 与速度曲线才在每端都成立
  （`USlideComponent::RuntimeState.LocalSlideStartTime`）。
- **不要在自己的 Tick 里 `SetComponentTickEnabled(false)`**：它会走到
  `FTickTaskLevel::RemoveTickFunction` / `AddTickFunction` **改 tick 列表**
  （`TickTaskManager.cpp:2434-2456`），而「滑行 / 蓄力这类动作的收尾」恰好常常发生在自己的 Tick 里。
  要么常开 Tick + 开头早退（`USlideComponent` 的做法），要么用延后手段（`SetComponentTickEnabledAsync`）。
- **本项目的实际构建链是 MSVC `cl.exe`，不是 clang-cl**：`Intermediate/.../<文件>.cpp.obj.rsp` 是
  MSVC 形态（`/Yu` `/Fp` `/experimental:log` `/sourceDependencies`，且**没有** `/clang:`、`/imsvc`），
  2.5GB 的 SharedPCH `.pch` 也是 MSVC 造的（magic `VCPCH0`）。把这份 rsp 交给 clang-cl 会**在解析源码之前**
  直接死在 `input is not a PCH file: ... file doesn't start with precompiled file magic`——
  看着像编译失败，其实一个源码错误都没查。clang-cl 在本项目里只服务 clangd（clangd 自建 preamble、
  忽略 `/Yu`/`/Fp`，所以 IntelliSense 一直是好的）。
  ⇒ 想做「真编译级」单 TU 检查，用 **`cl.exe @obj.rsp /Zs`**（保留 `/Yu`+`/Fp`），不要用 clang-cl。见 bug-039。
- **`cl.exe @obj.rsp /Zs` 是本机最省的单 TU 检查**：`/Zs` 只做语法检查、不写 `.obj`，所以 `/c`、`/Fo…`
  留着也无害（不产出文件）；cwd 必须是引擎源码目录（**`Shared.rsp` 里的 `/I` 是相对路径**）。
  仍要做**对照**：字节级复制源码（`Copy-Item`）后用 `AppendAllText`（UTF-8 无 BOM）追加一行
  `int WfSyntaxProbe = ;`，必须精确报出那 **1** 个错误（`PROBE_ERRORS=1`，行号 = 原文件行数+1）
  才说明这条检查真的在编译该 TU。
- **UHT 一重跑，clangd 就可能对「包括没改过的文件」报 `unknown type name 'FID_<file>_<line>_DELEGATE'`**：
  这是**虚警**（见 bug-014）。判据有两条：① 报错的那份 `*.generated.h` 里其实**有**该行号的宏定义
  （`grep FID_.*_<line>_DELEGATE`）；② **未被本次改动触及**的文件（如 `LandRollComponent.cpp`）
  会一起报同样的错。此时不要改代码，让 clangd 自己重解析（重开文件 / 等索引刷新）即可。
- **序列动画的根运动只有两个下场：被提取，或者留在姿态里**——抹掉根骨骼的唯一条件是
  `(bExtractRootMotion && bEnableRootMotion) || bForceRootLock`（`AnimationDecompression.cpp:274` 运行时压缩数据路径、
  `AnimSequence.cpp:1841` 编辑器 raw 路径），而 `bExtractRootMotion = ShouldExtractRootMotion()`
  只看**动画蓝图**的 Root Motion Mode（`AnimInstance.h:433`：仅 `RootMotionFromEverything` / `IgnoreRootMotion` 为真；
  序列播放器 `AnimNode_SequencePlayer.cpp:137`、混合空间播放器 `AnimNode_BlendSpacePlayer.cpp:134` 都传它）。
  ⇒ 在默认的 `RootMotionFromMontagesOnly` 下，**带位移的序列（爬行这类循环动画）位移会原样留在姿态里**：
  模型随根骨骼前移、混合空间循环回第 0 帧时轨道重置 = 「走一段被拉回去一点」，而且每个方向一条动画就每个方向都拉。
  四档语义见 `AnimEnums.h:29-44`：`NoRootMotionExtraction`＝Leave root motion in animation（漂移）、
  `IgnoreRootMotion`＝提取但不施加（真原地）、`RootMotionFromEverything`＝提取并施加（引擎注释点名**不适合联机**）、
  `RootMotionFromMontagesOnly`＝联机友好但序列不提取。见 bug-043。
- **动画编辑器预览和游戏里跑的不是同一套根运动判据**：单节点预览把资源自己的 `bEnableRootMotion` 当提取标志
  （`AnimSingleNodeInstanceProxy.cpp:324`），AnimBP 走的是模式判据 `ShouldExtractRootMotion()`。
  所以「在编辑器里勾上 Enable Root Motion 就看着正常」是预览现象，进游戏该漂还是漂——排障时别拿预览当证据。见 bug-043。
- **蓝图子类对类默认值的覆盖是在 C++ 构造函数跑完之后才应用的**：在构造函数里读自己的 `UPROPERTY`
  （如 `Inventory->InitializeContainer(InventoryCapacity)`）拿到的永远是 **C++ 初始值**——BP 里改的
  类默认值不起作用，而且不报任何错（静默失效）。要把硬编码值做成「策划可改」，消费点必须挪到属性
  初始化之后（`PostInitializeComponents` / `BeginPlay`）。客户端实例会跑同一个构造函数，所以搬家时
  别忘了加 `HasAuthority()` 判据——否则客户端调 `BlueprintAuthorityOnly` 函数会被权威门禁记 ERROR。
- **`COND_InitialOnly` 是本项目「静态调参」的标准复制条件**：`CoreNetTypes.h:22` 的注释就是
  「This property will only attempt to send on the initial bunch」，`RepLayout.cpp:1442` 在 `!bIsInitial`
  时直接跳过；`DOREPLIFETIME_CONDITION` 的 static_assert 只拦 `COND_NetGroup`（`Net/UnrealNetwork.h:277-283`），
  所以它是合法参数。出生束在客户端**先于** `AActor::PostNetInit` 应用（`Actor.h:2956-2957`
  「Always called immediately after spawning and reading in replicated properties」）⇒ 想要一个
  「必已收到这些值」的客户端钩子，就用 `PostNetInit`（`APlayerCharacter` 的调参验证日志挂在这里）。
- **`UDeveloperSettings` + `UCLASS(Config = Game, DefaultConfig)` 落在 Project Settings > Project > Game**
  （容器 "Project"、分类 "Game"，`DeveloperSettings.cpp:18-63`），序列化进 `Config/DefaultGame.ini`（入库）；
  每台机器的覆盖层是 `Saved/Config/<Platform>/Game.ini`（`ConfigContext.cpp:1007` 载入层级、`:1029` 合并），
  **不进 git**。`GetDefault<UXXX>()` 拿到的就是 CDO ⇒ 改 ini 后重启进程即生效，不用重编译、不用重新打包
  ——这是 DataAsset 做不到的（DataAsset 改了要重新打包）。
- **客户端的进程里也有同一份 ini 和同一个 CDO，所以「只有服务器读」是代码纪律而非引擎约束**：
  打包后每个玩家本地都有游戏目录，ini 是玩家可编辑的文件；一旦客户端侧代码用 `Get()` 的值去驱动玩法
  （速度 / 冷却 / 伤害），改自己那份 ini 就等于开挂。⇒ 任何 `Settings->X` 的**写**都必须过
  `IsAuthoritativeForActorComponent(this)`，客户端只认复制下来的那份值。
- **配置类按归属拆分：一个组件/区域一个 `UDeveloperSettings` 子类**（本项目在
  `Public/Character/Settings/` 下），**纯 header、没有 .cpp**——`GENERATED_BODY()` + 类内内联
  `static const UX *Get() { return GetDefault<UX>(); }` 就够了。`meta = (DisplayName = "…")` 同时是
  Project Settings 里的页名和该类的 ini 节名（`[/Script/Wildforge.<类名>]`，`GetSectionText()` 默认取它）
  ⇒ 拆成几个类就是几个互不干扰的 ini 节。字段用**短名不加前缀**（前缀由「属于哪个类」承担）。
- **`UDeveloperSettings` 的字段改名会静默丢配置**：ini 键名就是属性名，改名之后旧 ini 里的键
  既不会报错也不会生效（被当作未知键忽略），现象是「改了名以后打包服务器上的调参全部回到默认值」。
  改字段名时要同步改服务器上的 `Saved/Config/<Platform>/Game.ini`。**改类名同理**——ini 节名是
  `[/Script/Wildforge.<类名>]`，拆类/改名都会让旧节整节失效（旧键留在文件里但不被读取）。

## Do-Not-Repeat

<!-- Mistakes made and corrected. Each entry prevents the same mistake recurring. -->
<!-- Format: [YYYY-MM-DD] Description of what went wrong and what to do instead. -->
<!-- ⚠️ `.wolf/hooks/pre-write.js` 按本标题（## Do-Not-Repeat）硬编码切分，不可改名或搬走。 -->

- [2026-10-07] **任何「写回基准值」的复位函数，写之前必须先确认基准值已捕获**：基准值的默认 0
  与「角色不能动」的合法速度 0 无法区分，未捕获就写回会把角色永久钉在原地（本项目第一次攻击
  就这样锁死了双方角色）。用显式标志位（`bBaseSpeedValid`）+ 写入口的兜底闸门，不要靠数值判断。
  见 bug-027。
- [2026-10-07] **校验规则必须与实现接受的范围一致**：参数「负数 = 用默认值」这种约定，如果
  校验里写 `X < 0 就拒`，而实现里写着 `X < 0 ? 默认值 : X`，两者直接矛盾，蓝图 pin 的默认 -1
  会让**每一个请求都被拒**（本项目 24/24 全灭）且只留一条 WARNING。关掉默认值的合理性：先读
  实现，再写校验。见 bug-028。
- [2026-10-07] **不要用 `DisableMovement()`（MOVE_None）做「动作期间的输入门控」**：它会丢弃根运动
   （`CharacterMovementComponent.cpp:2716-2734`）并把跑动速度清零，靠位移的动作会变成原地滚 + 急停；
   更糟的是如果这个锁只在服务器落（客户端没有对应事件），客户端还在以跑速前进，误差累积后被位置纠正
   拉回来 = 橡皮筋。要「占住角色但不关移动」时用**软锁**（只记账 / 复位加速），位移留给根运动或蓝图。
   见 bug-029。
- [2026-10-07] **不要在 `NetMulticast` 的 `_Implementation` 里写「非权威端就忽略/报错」的门禁**：每个端
   都会执行它，客户端那一份正是**正常接收**（`Actor.cpp:5500-5519`），加门禁等于客户端永远看不到表现。
   （这条规则当时只改了翻滚，加速 / 闪现 / 攻击特效三处漏了 —— 见 bug-033，说明「立了规则」不等于
   「存量代码都合规」，改一个组件的同类代码时要顺手 grep 一遍其他组件的同名模式。）
   同理，**生命周期广播不要拿 `COND_OwnerOnly` 的复制属性当判据**（模拟代理永远收不到、永远是 false），
   要用一份各端自己维护的、不复制的表现标志，并让 Started / Finished 严格配对（表现没起来就一个都不广播，
   否则订阅者「只落锁、没人解锁」）。见 bug-030。
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
- [2026-10-06] **状态机里不要硬编码「默认外观」**：默认值要在第一次染色**之前**从设计器/初始值抓一次
  （如 `RefreshSlotStyleColor()` 里的 `SlotStyle->GetBrushColor()`），否则「第一次进入该状态」就是
  外观被永久改掉的时刻，而且 `UBorder::SynchronizeProperties` 重建时回放的正是被写坏的值。见 bug-015。
- [2026-10-06] `UFUNCTION` 里**不要把输出引用参数放在带默认值的参数之后**（如
  `BlinkForward(float Distance, float MaxDistance = 1200.f, …, FVector& Out)`）：C++ 规定默认值一旦开始
  后面都得有默认值，编译器会报 `missing default argument`，而 UHT 完全不查这条。把输出引用放第一个参数。见 bug-016。
- [2026-10-06] 用到只被**前置声明**的引擎类型时**不要只靠 Character.h 之类的间接包含**：拿
  `UCapsuleComponent` 的成员函数就必须 `#include "Components/CapsuleComponent.h"`，否则绕开 SharedPCH 的
  语法检查报 incomplete type。同 bug-014。
- [2026-10-07] **不要**给 `FTimerManager::SetTimer` 传 `0` 当「每帧」用（引擎语义是**清掉定时器**，一次都不会
  触发）；每帧定时器要用 `SetTimerForNextTick` + 回调里续挂。见 bug-017。
- [2026-10-07] **不要**把 Enhanced Input 的 `Completed` 当「按键松开」信号：Hold 触发器勾了 Is One Shot、或 Tap
  触发器按超时，都会在手没松时发 Completed。要么取消 One Shot（并把 Tap 的 `Triggered` 当点按），要么自己用
  阈值定时器 + 真正去查按键状态。见 bug-018。
- [2026-10-07] **不要在客户端会跑到的地方直接写玩法状态**（动画通知 / UI 回调 / Tick 都在客户端跑）：
  `AnimNotifyCombo` 曾直接 `AttackComp->bCanCombo = true`，改的是本地副本、服务器那份始终 false，联机下连击
  静默失效。这类写入一律走 `Server_*` RPC。**加新的动画通知 / 客户端回调时先问：这行改的是玩法状态吗？**
  见 bug-019。（同类误用已第三次：bug-007 拖拽 `SwapSlots`、bug-008 整理按钮、bug-019 连击窗口。）
- [2026-10-07] **连击的节流单位是「连击窗口」，不是「蒙太奇段」**：不要加「这一段只收一次
  输入 / 必须松开再按」这类标志来防连点——起手那次输入之后，整段蒙太奇期间都不会有新的
  按下事件来复位它，于是窗口一开所有点击全被拒，连击永远接不上（实测一次窗口连拒 4 次）。
  窗口每段都会被 `UAnimNotify_Combo` 重开，本身就是「一次机会」；接招后把 `bCanCombo` 置
  false 即可。只留 `ComboMinInterval` 防篡改客户端在同一窗口刷包。见 bug-022。
- [2026-10-07] **不要用整文件 write 去重构已有类**（本项目已踩一次：`bCanAttack` 在重写
  `AttackComponent` 时被静默丢掉，用户自己发现的）：编译器不会提醒丢了没被引用的成员，蓝图里已连的引脚
  也只在打开资产时才报错。要重写就先把原文件的 UPROPERTY/UFUNCTION 清单列出来，逐项核对在新版里是否
  仍然存在（保留 or 明确说明删除理由）。见 bug-020。- [2026-10-07] **不要把连击做成「等本段动画播完再接下一段」**（用户明确纠正过两次）：连击通知就在蒙太奇
  播放中途发出，窗口一开点击就该**立刻切播下一段**。同理不要用「本段正在播」当拒绝输入的理由
  （曾用 `bAttackMontageInProgress` 在最前面 return，把连击全挡掉，见 bug-019）；防连点要用
  「每段只收一次输入、必须松开再按」这种显式标记，而不是把整段时间锁死。- [2026-10-07] **不要**假定「蒙太奇会在所有端播」：引擎默认只复制 RootMotion（本项目未启用 GAS），
  只在服务器 `Montage_Play` 的话客户端看不到动画、`OnMontageEnded` 不触发，挂在结束回调上的收尾逻辑
  （恢复移动等）在客户端全部失效。要同步表现必须自己 `NetMulticast`，见 bug-019。
- [2026-10-07] **重写/搬运组件头文件时不要靠「以前编得过」推断 include 齐全**：把代码从 Actor 搬到
  UActorComponent、或整文件重写组件头文件，最容易丢的是 `TimerManager.h`（`Engine/World.h` 只前置声明
  `FTimerManager`）、以及把 `GetWorldTimerManager()`（AActor 的方法）一起搬过来。见 bug-023。
- [2026-10-07] **`BlueprintNativeEvent` 的参数类型不要写 `TEnumAsByte<T>`**：UHT 生成的
  `_Implementation` 声明用的是裸枚举类型，头文件必须逐字一致，否则编译期报
  `out-of-line definition ... does not match any declaration`（UHT 自己不会报）。见 bug-023。
- [2026-10-07] **`UPROPERTY(BlueprintReadOnly/BlueprintReadWrite)` 不要放在 `private:` 区段**：UHT 以
  `-WarningsAsErrors` 运行，直接报 `BlueprintReadOnly should not be used on private members`。
  想留在 private 就去掉蓝图可见性；否则挪到 protected。见 bug-024。
- [2026-10-07] **多方共享的「禁止移动」锁不要用计数，用按来源记账的集合**：计数在「同一来源重复
  请求」时会泄漏（连击切段重复广播 `OnAttackStarted` 就会让锁回不到 0），集合的重复 Add 幂等。
  判断一个共享状态该用哪种抽象，先问「需不需要区分是谁在持有」。见 Decision Log。
- [2026-10-07] **不要用 last input vector 在服务器上判移动方向**：它只由客户端累加，服务器上恒为零，
  判定会静默失效（不是报错，是「判定结果永远是没输入」）。服务器读 `Acceleration`
  （`GetCurrentAcceleration()`），并且**不要用它的长度当输入强度**——长度被引擎填成 1，不是玩家推的力度。
  见 bug-026。
- [2026-10-07] **不要再假设「移动参数（`MaxWalkSpeed` / `GroundFriction` / `MaxAcceleration`）会复制」**：
  UE 5.7 的 `UCharacterMovementComponent` 一个都不复制（见 Key Learnings / bug-031）。改这些参数的组件
  必须自己把同一份值写到**本地控制的自主代理**上，否则客户端预测与服务器不一致 = 橡皮筋。
- [2026-10-07] **不要让客户端自己推进或自己提前结束权威速度曲线**：客户端的 `MaxWalkSpeed` 是
  服务器复制下来的镜像值（`USprintBoostComponent::bMirrorSpeedOnOwningClient` / `OnRep_*` +
  Tick 兜底），本地算一套、或本地按方向门控提前收掉，都只会让它与服务器不一致 = 自造偏差。
  曲线、方向门控、结束条件**只在权威端跑**。见 bug-031。
- [2026-10-07] **不要给同一个状态留两份同名字段（一份复制的、一份私有的权威副本）**：
  私有那份在客户端永远是初始值，而复制那份若没人写也永远是 false/0，两者一起就把
  「客户端读到的状态」变成假值（`IsSpeedBoostActive()` 恒 false、`GetSpeedBoostAlpha()` 恒 0）。
  「加速中」这类状态只保留**复制的那个 UPROPERTY** 一份，权威端也写它。见 bug-032。
- [2026-10-07] **不要让第二个组件去「快照-改写-还原」`MaxWalkSpeed`**：`USprintBoostComponent` 已经把它
  当成独占资源（`CaptureBaseMaxSpeed` / `ResetMaxSpeed`），两方各存一份快照就会出现
  「另一方中途复位 → 这一方收尾时把过期值写回去」→ 角色永久带着错误速度（bug-027 的翻版）。
  新的位移类能力（滑行）改为「零摩擦 + 自己夹速度 + 只用 `MaxAcceleration` 调转向」，一个 `MaxWalkSpeed` 都不碰；
  真要改速度，走 `USprintBoostComponent` 的公开 API（`SetBaseMaxSpeed` 等）而不是直接写字段。
- [2026-10-07] **不要在自己的 Tick 里 `SetComponentTickEnabled(false)`**（会改 tick 列表，
  `TickTaskManager.cpp:2434-2456`）：需要逐帧推进的组件用「常开 Tick + 开头早退」，不要动态开关。
- [2026-10-07] **「本端进度 / 曲线」不要用复制的服务器时间戳算**：各端 World 时间起点不同，
  收到表现同步时自己记一份本端起算时间（`USlideComponent::RuntimeState.LocalSlideStartTime`）。
- [2026-10-07] **看到 clangd 报 `unknown type name 'FID_..._<line>_DELEGATE'`（或 `a type specifier is required`）
  时不要改代码**：UHT 刚重跑过的话这是虚警——先查那份 `*.generated.h` 里是否有该行号的宏定义，
  再看**没改过的文件**是否也一起报；是就等 clangd 重解析。见 bug-014。
- [2026-10-07] **不要把本项目的 `obj.rsp` 交给 clang-cl 做单 TU 检查**：那是 MSVC 形态的 rsp，
  它配套的 `.pch` 是 cl.exe 造的，clang 会报 `file doesn't start with precompiled file magic`
  并**在解析源码前退出**（等于什么都没检查）。要用 `cl.exe @obj.rsp /Zs`。见 bug-039。
- [2026-10-07] **内存吃紧（编辑器 + clangd 常驻）时不要反复重跑注定失败的 UBT 构建**：`cl.exe` 会报
  C3859 / C1076，而 UBA 会**无限重试**——每轮都往 `Log.txt` 追加（实测涨到 441MB），一直占 4GB 内存，
  并且**始终持有 UBT 互斥量**，于是并行的 `Build.bat` 既不编译也不报错、只是静静阻塞（看起来像命令卡死）。
  先看 `FreePhysicalMemory` / `FreeVirtualMemory`，必要时 `taskkill` 掉卡住的 `UnrealBuildTool` 进程。见 bug-039。
- [2026-10-07] **不要用「取消勾选动画的 Enable Root Motion」去消除序列的漂移/回拉**：抹掉根骨骼的条件是
  `bExtractRootMotion && bEnableRootMotion`（`AnimationDecompression.cpp:274`），关掉这个开关就是**永远不抹**，
  游戏里照样漂；正确做法是改动画蓝图的 Root Motion Mode（想原地就 `Ignore Root Motion`）或把动画本身做成原地。
  见 bug-043。
- [2026-10-07] **不要用 `Root Motion From Everything` 做联机项目的位移来源**：引擎自己的枚举注释就写着
  「not suitable for network multiplayer setups」——服务器与客户端各自本地播动画、相位不同步，提取出来的位移
  会在两端分叉，等于把 bug-029 的橡皮筋请回来。速度驱动的位移交给移动组件（本项目爬行＝`ProneMaxWalkSpeed`）。见 bug-043。
- [2026-10-08] **配置类的字段名和消费点要一次对齐**：新建/改某个 `<X>Settings` 时，
  每个 `bOverride_X` + `X` 都必须同时与组件里的 `Settings->bOverride_X` / `Settings->X` 对上。
  UHT 与 ini 都不校验键名，编译器只拦「这个名字根本不存在」，而**名字像但不是同一个**
  （当时的 `bOverride_StopMovementOnStart` vs `bOverride_LandRollStopMovementOnStart`）在两边都是新写的时候
  极易发生——那次的根源是「同一个类里塞了多个组件的配置，只好靠前缀区分，而前缀漏了一处」；
  现在改成一类一组配置、短名不加前缀，**同一类里不再有前缀问题**，但「消费方是不是引用了正确的那个类」
  成了新的检查点（改 include / 类名时 `grep -rn "Settings->"` 与**对应那个**头文件的声明逐项对照）。见 bug-052。
- [2026-10-08] **不要在客户端侧用 ini 的值改组件字段**：客户端读的是**玩家本地那份 ini**
  （可被改），拿它去改速度/冷却就是让客户端说了算，而且两端会分叉。组件里的
  `ApplyGameplaySettingsOverrides()` 一律以 `IsAuthoritativeForActorComponent(this)` 开头，
  客户端那份值只来自 `COND_InitialOnly` 复制。
- [2026-10-08] **在一个 `.cpp` 里改动多处时不要靠「行号记忆」定位**：本会话里 `SlideComponent.cpp`
  的 `COND_OwnerOnly` 行（`SlideCooldown`）与新增的 `COND_InitialOnly` 行相邻，插错位置会让
  同一个属性注册两次（UHT 不报错、运行期行为诡异）。加复制注册时先 `grep -n DOREPLIFETIME` 看全貌。

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
  **「未选中/未高亮」的着色不是常量**，而是第一次染色前从设计器抓下来的 `NormalSlotColor`
  （`RefreshSlotStyleColor()` 内懒抓一次）：高亮是临时染色，退出必须回到进入前的样子，
  否则拖拽扫过的格子会被永久改外观（见 bug-015）。
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
- [2026-10-07] **攻击系统按「玩法状态 / 表现」分层定权威边界**（`UAttackComponent`，见 bug-019）：
  玩法状态（选段下标、连击窗口、`bIsAttacking`、命中结算）全部服务器写；表现（蒙太奇播放、特效）用
  `NetMulticast` 同步到所有端。客户端只能 `Server_Attack` / `Server_NotifyComboWindow`。
  两个通知（`OnAttackStarted` / `OnAttackFinished`）刻意做成「每端各自本地广播」——订阅者（如
  `APlayerCharacter` 禁止/恢复移动）能就地立即响应，不必等状态复制往返；代价是必须在 Multicast 里广播
  （而不是只在服务器），否则客户端收不到。**禁止移动/恢复移动必须每端各自做**：移动模式是本地瞬时状态，
  等服务器复制会抖。
- **连击 = 「窗口开着时点击，立刻切播下一段动画」，不要做成「排队等本段播完」**（用户纠正过两次）：
  连击通知（`UAnimNotify_Combo`）就是在蒙太奇**播放中途**发的，把它当成「等动画播完再接」等于永远接不上。
  正确做法是窗口内点击直接 `Montage_PlayWithBlendIn` 切下一段（淡入时长用上一段已播时长，0.05~0.2s），
  `bIsAttacking` 保持 true 所以移动锁不解；切段时引擎会触发上一段的 `OnMontageEnded(bInterrupted=true)`，
  要用 `bAdvancingCombo` 标记把它与「攻击链真的结束」区分开。**连击不受 `AttackCooldown` 约束**——那是管
  「两次起手之间」的，拿它卡窗口会让开得早的窗口被无声拒绝；连击只留 `ComboMinInterval` 做防刷包节流。
- [2026-10-07] **能力做成组件的边界：组件自带它的复制通道与表现，宿主类只做装配**。加速与闪现原先写在
  `ABaseCharacter` 上（`FSprintBoostState` + `BlinkForward` + 4 个 RPC），现已拆成
  `USprintBoostComponent` / `UBlinkComponent` 挂到 `APlayerCharacter`——基类只保留「所有角色都存在」的
  东西（`bReplicates` + `UCharacterAttributes`）。理由：能力的权威状态、复制通道、蒙太奇、RPC 入口都是
  能力自己的事，放基类会让**每个**派生类都背一遍组件的开销与复制通道。
  **代价（用户已知情并选择）**：旧蓝图节点（BP_ThirdPersonCharacter 里的 `Server_StartSpeedBoost` /
  `Server_StopSpeedBoost`）需要手动重连到 `GetSprintBoostComponent()`，本次**没有**写 CoreRedirects。
  player 类现在同时持有 6 个组件，访问器是 `GetInventory` / `GetAttackComponent` /
  `GetLandRollComponent` / `GetSprintBoostComponent` / `GetBlinkComponent` / `GetSlideComponent`。
- [2026-10-07] **加速 / 闪现的可选蒙太奇做成「允许为空」的合法状态**：内容仓库里没有冲刺与闪现的蒙太奇
  资源（`Content/Characters/Man/Animations/Montage/` 下只有 `LandRollMontage` + 4 个攻击蒙太奇），
  所以新组件的 `SprintMontage` / `BlinkMontage` 默认空，播放时记一条 **INFO**（不是 WARNING——
  空配置是设计内状态）后跳过，速度曲线与位移完全不受影响。这两个组件**都不订阅** `OnMontageEnded`：
  加速的结束条件是速度曲线 / 松键，闪现是一次性位移，都不该被动画生命周期反向控制
  （`ULandRollComponent` 则必须订阅——翻滚的收尾本来就由动画驱动）。
- [2026-10-07] **攻击与翻滚共用一套移动门控，用「按来源记账的集合」而不是布尔或计数**（`APlayerCharacter`
  的 `TSet<FName> MovementLockHolders`）：只在集合 0→1 时 `StopMovementImmediately + DisableMovement`、
  回到 0 时才 `SetMovementMode` 恢复。**布尔**（`bMovementLockedBy…`）无法区分持有者，攻击结束会把
  翻滚的锁一起放掉（不崩不报错，只表现为「翻滚没结束就能走」）；**纯计数**（`int32 MovementLockCount`，
  我第一版写的）能区分数量但不能区分来源，而攻击组件在**每次连击切段**时都会重复广播
  `OnAttackStarted`（一次 3 段连击 = 3 个 Started + 1 个 Finished），计数会泄漏到锁不回来 = 角色永久不能动；
  集合的重复 Add 天然幂等，且 `DescribeMovementLockHolders()` 能直接打出「当前被谁锁着」。
  **前置条件**：持有锁的组件必须与角色同生共死（本项目都是构造函数里 `CreateDefaultSubobject` 的），
  运行时 `RemoveComponent` 掉持有锁的组件会让锁永远挂着——真要这么做必须先主动解锁。
  **同日修正（见 bug-029）**：翻滚不再用硬锁 —— `ApplyMovementLock(Source, bDisableMovement)` 分两种强度，
  攻击 = 硬锁（`StopMovementImmediately + DisableMovement`），翻滚 = **软锁**（只记账 + 复位加速，不碰移动
  模式与速度）。因此「恢复移动」的判据从「全部持有者清空」改成新增的硬锁子集
  `MovementDisablingHolders` 清空，并且用 `bMovementDisabledByLock` 显式记录「移动组件是被这套锁禁掉的」，
  不再靠 `MovementMode == MOVE_None` 反推（它可能来自布娃娃 / 死亡 / 过场）。
- [2026-10-07] **权威门禁宏抽成 `Utils/ComponentAuthorityGuard.h` 的 `WF_COMPONENT_AUTHORITY_GUARD`**：
  与 `ItemContainer` 的 `WF_CONTAINER_AUTHORITY_GUARD`、`AttackComponent` 的 `WF_ATTACK_AUTHORITY_GUARD`
  语义完全一致（非权威端 WFLOG_ERROR + 安全返回，void 用 `void()`），新组件统一用它，不再各写一份宏。
- [2026-10-07] **「只有向前移动才加速」的方向门控写在 C++ 组件里，不在蓝图**（`USprintBoostComponent`
  的 `bSprintOnlyForward` + `MaxForwardSprintAngle`，默认开启 / 60°）：`MaxWalkSpeed` 是服务器权威的，
  只在客户端蓝图判方向的话，转向后服务器还在给加速速度、要等一个 RTT 才收（表现为「方向已经转过去、
  加速还挂着」）；而且服务器根本读不到 last input vector（见 Key Learnings / bug-026），蓝图那条路
  在联机下会静默失效。方向判定每个定时器回调跑一次（不是只在起手判一次）——到顶后 `bBoostActive`
  仍是 true、速度仍保持，只在起手判定会变成「到顶后转向侧后方 = 无限时长朝后加速」。判定放行四种情况
  都是防误伤：功能关着、**本帧没有移动输入**（先按 Shift 再按 W 是常见操作顺序）、非 `MOVE_Walking`
  （空中不判定）、没有移动组件。判定集中在一个 `EvaluateSprintDirection()` 里，供门控 / 调试串 /
  蓝图查询三处共用，避免实现漂移。可选表现留白：想「把冲刺速度改成按方向缩放」而不是直接收掉时，
  不要动这个门控（它是「允许/不允许」的判定），另外加一个方向缩放因子。
- [2026-10-07] **`USprintBoostComponent::ResetMaxSpeed()` 改为幂等安全复位，并显式区分「基准值是否已捕获」**
  （`FSprintBoostState::bBaseSpeedValid`，只有 `CaptureBaseMaxSpeed()` 会置 true）：未加速时它**不碰
  `MaxWalkSpeed`**，只清定时器/状态。原因见 bug-027 —— 基准值默认 0 与「角色不能动」的 0 无法区分，
  而 `APlayerCharacter` 在每次攻击/翻滚落锁时都会调它，无条件写回等于把角色永久钉死。同时
  `ApplyMaxWalkSpeed` 加了「速度 <= 0 或非有限值就记 ERROR 并拒写」的兜底闸门：速度提升永远不需要
  非正值，这类误用必须立刻可见而不是静默把角色锁死。
- [2026-10-07] **翻滚的「表现生命周期」与「玩法状态」用两套状态分离**（`ULandRollComponent`，见 bug-030）：
  `bIsRolling` / `LastLandRollTime` / 冷却 / 累计次数继续由服务器写、`COND_OwnerOnly` 复制（服务拥有者客户端
  的 UI / 调试）；而 `OnLandRollStarted` / `OnLandRollFinished` 由**不复制**的本端 `bRollPresentationActive`
  驱动——它在 Multicast 分支「表现真的播起来之后」置位并广播 Started，在收尾时复位并广播 Finished，两者严格
  配对。选它的理由：移动门控是**每端本地**的，必须每端都能就地落锁/解锁、不等一个 RTT；而 `COND_OwnerOnly`
  的属性在模拟代理上永远读不到，拿它当判据会让别人的客户端只落锁不解锁。顺带加 `IsRollPresentationActive()`
  给动画蓝图/UI 用（在所有端都有效），`bIsRolling` 则明确标注「只有拥有者客户端读得到」。
- [2026-10-07] **表现同步的 Multicast 一律不在 `_Implementation` 里加权威门禁**（翻滚的特效与蒙太奇都按这条
  改）：Multicast 在每个端都会执行，客户端那一次是正常接收，门禁只会让客户端永远看不到表现。要在实现里做
  区分，只能区分「这个端该做什么」（权威端写状态、非权威端只播表现），不能区分「是谁调用的」。
- [2026-10-07] **滑行（`USlideComponent`）的位移模型刻意绕开 `MaxWalkSpeed`**：用「`GroundFriction` /
  `BrakingDecelerationWalking` 置 0（引擎据此跳过刹车，`CharacterMovementComponent.cpp:4310-4316`）
  + 起手一个冲量 + 每帧把水平速度往下夹到自己算的曲线」，转向只给一点 `MaxAcceleration`。
  理由：`MaxWalkSpeed` 已被 `USprintBoostComponent` 独占（见 Do-Not-Repeat），而滑行需要「动量优先、
  可被撞墙自然打断」的速度曲线，这套模型同时在**权威端与本地控制的自主代理**上跑（本地预测镜像，
  因为移动参数不复制，见 bug-031）。结束时机（时长 / 速度 / 离地）**只由权威端 Tick 判**，
  客户端等 `Multicast_EndSlide`，避免 RPC 延迟里的「我这端已经结束、服务器还在滑」。
- [2026-10-07] **滑行不订阅 `OnMontageEnded`，蒙太奇为空是合法状态**：结束条件是速度曲线 / 时长 / 离地，
  不是动画播完（与 `USprintBoostComponent` 同类，而与必须靠动画收尾的 `ULandRollComponent` 相反）。
  因此 Started / Finished 的配对**不看蒙太奇是否播起来**，只看 `bSlidePresentationActive`——
  照抄翻滚那条「表现没起来就不广播」的规则会让没配蒙太奇时整个能力失效。
  `OnSlideStarted` 在 `Multicast_BeginSlide` 里**先于**移动参数改写广播：让订阅者（角色类）的软锁 /
  加速复位先发生，否则收尾时会把它复位掉的旧值当成自己的快照写回去。
- [2026-10-07] **bug-031 的修法选「自己复制 + 拥有者镜像」，不选「自定义移动组件子类」**：
  `USprintBoostComponent` 用 `ReplicatedMaxWalkSpeed{,Crouched}`（`COND_OwnerOnly`）把权威速度发给
  拥有者，拥有者在 `OnRep_*` 里写自己的移动组件，Tick 再兜底核对一次（值一致时只是两次浮点比较）。
  考虑过并否决的两条路：① override `UCharacterMovementComponent::GetMaxSpeed()` 读复制值——那是
  「更正确」的做法（Lyra 就是走 GAS 属性 + 自定义移动组件），但要换掉 `ACharacter` 的移动组件类，
  波及所有蓝图角色的组件引用与 `FindComponentByClass` 调用点，收益不抵代价；② 让客户端按自己的时钟
  跑同一套曲线——相位差（RTT/2）与时钟起点差异会让它比镜像值更差，而且一旦本地提前结束就变成持续偏差。
  保留 `bMirrorSpeedOnOwningClient=false` 只是为了双人 PIE 里做对照（关掉就能看到纠正次数暴涨）。
- [2026-10-07] **趴下（`UCrawlingComponent`）改速度只走 `USprintBoostComponent::SetBaseMaxSpeed()`**，
  不自己快照 / 直写 `MaxWalkSpeed`（那是 Do-Not-Repeat 里明令禁止的第二份快照）。它会把新速度同时写进
  复制镜像（`ReplicatedMaxWalkSpeed{,Crouched}`），拥有者客户端的预测因此一起变慢，不橡皮筋；
  宿主没有该组件时才降级为直写并记 WARNING（说明客户端不会跟随）。抓基准速度必须在**复位加速之后**，
  并用 `bProneBaseSpeedCaptured` 显式标志区分「还没抓过」与「基准真的是 0」。
- [2026-10-07] **趴下用两个标志，不合并**：`bIsProne`（姿态，**不带 condition**，因为每个端的动画蓝图
  都要靠它把角色保持在趴下姿态）+ `bCrawlTransitionActive`（过渡状态，`COND_OwnerOnly`）。
  移动锁（`MovementLockCrawl`，软锁）只锁「这一次过渡动画」，动画 BlendOut/结束即释放——
  趴着仍能以爬行速度移动。合并成一个标志会让「趴着发呆」和「正在过渡」互相污染（bug-030 的形态）。
- [2026-10-07] **蒙太奇播不出来时的取舍按能力分别定**：趴下**保留**姿态与爬行速度（姿态是目的，
  没动画也该趴下去），起立没动画则直接站起并还原速度；这与 `ULandRollComponent`「播不出来就回滚」相反，
  也与 `USlideComponent`「蒙太奇为空是合法状态」同类。两个方向的差异都写进了头文件注释，
  免得下一个人以为是漏改。
- [2026-10-07] **趴下/起身的移动门控用软锁，且不加「能力互斥」逻辑**：跨能力排斥（翻滚 / 攻击 / 滑行
  期间不能趴下）**不写进组件**，交给外部调用者用 `bCanCrawl`（总开关）与移动锁协调，
  组件之间保持互不引用；这是本项目一贯的组件解耦方向。
- [2026-10-07] **调参数值不放进 GameMode**：`AGameModeBase` 在联机里是服务器专属对象（客户端
  `GetWorld()->GetAuthGameMode()` 恒为 null，也没有复制通道），而本项目的能力/移动参数必须在两端同源
  （移动组件不复制参数，见 bug-031），所以能力参数留在各自组件的 `UPROPERTY` 上；要集中调参就用
  DataAsset / `UDeveloperSettings`（与 `UItemSystemSettings` 同一路子）。GameMode 只留给「一局 / 一张地图
  的规则」（昼夜、刷怪、掉落、复活…），且推荐只持有 DataAsset 引用、数值放资产里；客户端要展示的运行态
  必须经 GameState 复制。据此外提了 ②类硬编码：背包容量 → `APlayerCharacter::InventoryCapacity`
  （**权威端 BeginPlay 读取**，客户端等 Slots 复制）、网格列数 → `UItemContainerGrid::SlotsPerRow`
  （`InitializeGrid` 的 `InSlotsPerRow <= 0` = 沿用设计器值，调用方不再传 5）、连击切段淡入 →
  `UAttackComponent::ComboBlendInMinTime/MaxTime`。注意 RPC `_Validate` 里的上限（10000 / 60s / 100000）
  **不属于**这一类：那是协议可信边界，留在实现旁边。
- [2026-10-08] **调参三层落地：组件 `UPROPERTY`（类默认值 = 基线 + 兜底）→ ini（`UWildforgeGameplaySettings`，
  改完不重编译/不重打包）→ `COND_InitialOnly` 复制（客户端拿到同一份）**。选 ini 而不是 DataAsset：
  打包后的服务器上直接手改 `Saved/Config/<Platform>/Game.ini` + 重启即可调参，DataAsset 得重新打包。
  配套约定：只有权威端读 ini（客户端只认复制值）、不做运行时热重载（避免「同一次联机里新老角色数值不同」
  这种难查状态）、ini 值在消费点再夹一次（手写文本绕过了面板 Clamp）。
- [2026-10-08] **配置字段用 `bOverride_X`（`InlineEditConditionToggle`）+ 值，不用哨兵值**：
  bool 无哨兵值可用（-1 那套对 bool 无解），而「未勾选 = 不覆盖」在编辑器里一眼可见；
  代价是属性数量翻倍（40 项 → 80 个 UPROPERTY），换来的是二义性为零。未勾选时组件保留自己的
  类默认值，勾选后 ini 值在服务器生效并复制下来。
- [2026-10-08] **配置按归属拆成多个 `UDeveloperSettings` 子类（一类一页/一节），字段用短名不加前缀**：
  最初是全项目一个大 `UWildforgeGameplaySettings`（40 项 + 前缀防重名），用户否掉了这种「都挤到一起」
  的写法——可读性差、改一个组件要在一屏里找自己的那几项、每组还得靠前缀区分（bug-052 就出在这）。
  现在改成 `Character/Settings/<X>ComponentSettings.h` 一个组件一个类，Project Settings 里每个组件
  一个独立页、`DefaultGame.ini` 里一个独立节，字段名回到 `Cooldown` / `MontagePlayRate` 这种最短形态；
  代价是「同名不同义」由类的边界承担，读代码时必须先看是哪个 `Settings`（所以 include 一定要对）。
  仍然**不用嵌套 `USTRUCT`**：结构在 ini 里的序列化格式不如扁平属性确定可读。
- [2026-10-08] **调参项的复制条件用 `COND_InitialOnly`（而不是 OwnerOnly / SimulatedOnly）**：
  这些值在角色整个生命周期不变、且**每个端**都要读（动画 / 表现 / UI / 预测），
  只发 owner 会让旁观者用错值。附带修好一处旧问题：`UBlinkComponent::BlinkCooldown` 原先**没有任何
  复制通道**，客户端 `IsBlinkReady()` 读的是类默认值（ini 一改就两端不一致），现在随出生束下发。
  背包容量不需要复制——客户端容量由复制下来的 `Slots` 推导。
