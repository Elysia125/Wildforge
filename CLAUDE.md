# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## OpenWolf 上下文系统

@.wolf/OPENWOLF.md

本项目使用 OpenWolf 进行上下文管理。每次会话都要阅读并遵循 `.wolf/OPENWOLF.md`。读取项目文件前先查看 `.wolf/anatomy.md`；编写代码前先查看 `.wolf/cerebrum.md`（尤其是 `## Do-Not-Repeat` 章节）；修复 bug 前先查看 `.wolf/buglog.json`；改动后更新 `.wolf/anatomy.md` 并向 `.wolf/memory.md` 追加记录。OpenWolf 的 JS 钩子通过 `.claude/settings.json` 强制执行这些规则。

## 项目概览

Wildforge 是一个 **Unreal Engine 5.7** 的第一人称游戏（源自 First Person BP 模板），正在开发为生存/建造类游戏。当前活跃的功能区域是带网络复制的**物品/背包系统**（C++ 核心 + UMG 界面）。引擎安装在 `D:/UE/UE_5.7`，是仅限 Windows、使用 clang-cl 工具链的环境。

## 构建与开发命令

**本机的引擎路径、项目路径、工具链路径以及编译验证命令写在 `CLAUDE.LOCAL.md`（本机专属、不入 git）。**
每个开发成员的安装路径都不同，所以下面的命令只是通用说明；**实际验证请以 `CLAUDE.LOCAL.md` 里的命令为准**。
新增本地专属命令请写进 `CLAUDE.LOCAL.md`，不要写进本文件。

项目内没有自带的构建脚本，构建通过 `.vscode/tasks.json` 中的 VS Code 任务调用 UBT（引擎路径硬编码在任务里）：

- 构建编辑器目标（默认构建任务）：
  ```
  D:/UE/UE_5.7/Engine/Build/BatchFiles/Build.bat WildforgeEditor Win64 Development -Project=E:/UE_Projects/Wildforge/Wildforge.uproject -WaitMutex
  ```
- 为 clangd 重新生成 `compile_commands.json`（构建任务会先自动执行）：
  ```
  D:/UE/UE_5.7/Engine/Build/BatchFiles/Build.bat -Mode=GenerateClangDatabase -Project=E:/UE_Projects/Wildforge/Wildforge.uproject -Target=WildforgeEditor Win64 Development -OutputDir=E:/UE_Projects/Wildforge
  ```
  需要环境变量 `LLVM_PATH=E:/LLVM`。
- 启动编辑器：`D:/UE/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe E:/UE_Projects/Wildforge/Wildforge.uproject`。
- 对 Gameplay 模块的 C++ 改动可以使用 Live Coding / 编辑器内重编译。

本项目没有测试框架、linter 或 CI。验证方式是编译并运行编辑器。

**工具链注意：** VS Code 使用 clangd（`C_Cpp.intelliSenseEngine` 已禁用），读取根目录生成的 `compile_commands.json`。`.clangd` 会移除 `-Werror`/`/WX`，让 clangd 报告警告而非错误。`.clang-format` 基于 LLVM 的 UE 风格：`CoreMinimal.h`/PCH 排最前，`*.generated.h` 永远排最后。

## 架构

单个 Runtime 模块 `Wildforge`（`AdditionalDependencies: Engine, UMG`）。`Wildforge.Build.cs` 的公开依赖为：`Core, CoreUObject, Engine, InputCore, UMG, Slate, SlateCore`。注意运行时使用了 Enhanced Input，但 Build.cs 中**未**列出（依赖引擎默认传递依赖）。

整个系统是**服务器权威、面向网络复制**的。代码按 Public/Private 拆分，include 使用模块相对路径（如 `ItemSystem/...`、`Character/...`、`Core/...`）。注释和枚举显示名称均为中文。

**角色层**
- `ABaseCharacter`（`Character/BaseCharacter.h`）——继承 `ACharacter`，`bReplicates = true`，拥有 `UCharacterAttributes` 组件。
- `UCharacterAttributes`（`Character/Components/`）——使用 `DOREPLIFETIME` 复制（生命值对所有客户端可见，用于血条），并用 `ReplicatedUsing` 的 OnRep 广播 `FOnHealthChanged`/`FOnMaxHealthChanged` 供 UI 使用。Setter 标记为 `BlueprintAuthorityOnly`。
- `APlayerCharacter`（`Character/Player/`）——拥有 `UPlayerInventory`（初始化为 30 格）。声明了服务器 RPC（`Server_RemoveItemAtSlot`、`Server_RemoveItem`、`Server_SwapSlots`），其 `_Validate` 只做廉价的参数检查。**客户端从不向服务器发送物品数据（`FItemInformation`）**——以此防止伪造，真正的越界检查在容器内部完成。

**物品系统**（`ItemSystem/`）
- `FItemInformation`（`Structs/ItemInfo.h`）——继承 `FTableRowBase` 的物品定义；运行时定义来自 `Content/ItemSystem/DataTables/` 下的 `DT_Items` 数据表。
- `UItemContainer`（`Components/`）——系统核心。服务器权威复制数组 `Slots` / `SlotOccupied`（`COND_OwnerOnly` 复制），以及派生的非复制索引（`FreeSlots` LIFO 栈、`ItemIDToSlot`、`UsedCount`），客户端在复制时通过 `RebuildDerivedState()` 重建。所有修改函数均为 `BlueprintCallable, BlueprintAuthorityOnly`，并广播 `FOnContainerChanged`。包含增/删/交换/整理/调整大小/查询等 API。
- `UPlayerInventory`（`Components/`）——`UItemContainer` 子类，将容器类型固定为 `PlayerInventory`。
- `AItemMaster`（`Actors/`）——目前是空 Actor 桩，被物品数据以 `TSubclassOf<AItemMaster>` 引用。

**UI 层**（UMG，由 `BindWidget`/`BindWidgetAnim` 驱动）
- `UMainUserWidget`（`UI/`）——持有 `UInventoryUserWidget` 并切换其可见性。
- `AWildforgePlayerController`（`Core/`）——`ShowInventory()` 在 `FInputModeGameAndUI`（显示光标）与 `FInputModeGameOnly`（隐藏光标）之间切换。
- `UInventoryUserWidget`（`ItemSystem/UI/`）——在所属**Pawn**（而非 Controller）上查找 `UItemContainer`，初始化网格，并用 `WidgetSwitcher` 切换背包页（第 0 页）/ 合成页（第 1 页），带控件动画。
- `UItemContainerGrid`（`ItemSystem/UI/`）——`UniformGridPanel` + `ScrollBox`，订阅 `OnContainerChanged`，保持控件数量等于容量，并根据物品数据刷新格子。
- `UInventorySlotWidget`（`ItemSystem/UI/`）——单个格子；`BindWidget` 名称至关重要（必须与 UMG 中的名称完全一致）。

## 约定与坑

- `FItemInformation::ItemQuality` 在整个容器和 UI 中**兼作堆叠数量**（结构体没有单独的数量字段）。
- `ItemContainer` 依赖 UE 5.7 的 API（`EAllowShrinking::No`）；`RebuildDerivedState` 按容量→0 逆序迭代，使客户端的空闲槽弹出顺序与服务器一致。
- `Wildforge.Build.cs` 缺少 `EnhancedInput`，尽管代码中使用了 Enhanced Input 类——如遇链接错误请补上。
- `DefaultEngine.ini` 中启用的 GameMode 指向 FirstPerson 蓝图路径，而 `Content/Core/` 下还存在另一个 `BP_FirstPersonGameMode` 资源。
- `Config/DefaultGame.ini` 中仍是模板默认的 `ProjectName`（"First Person BP Game Template"）。

## 上下文文件分工（写东西之前先看这里）

`.wolf/` 与两个 CLAUDE 文件各有分工，**不要重复叙述同一件事**：

| 要记的内容 | 写到哪 | 写法要求 |
|---|---|---|
| 一次 bug 的完整因果 | `.wolf/buglog.json` | 结构化：`error_message` / `file` / `root_cause` / `fix` / `tags`；**因果细节只写这里** |
| 从 bug 提炼出的禁令 | `.wolf/cerebrum.md` 的 `## Do-Not-Repeat` | **一到两行**，只写"不要做什么 + 正确做法"，并**交叉引用 bug 编号**（如 `见 bug-007`），不要复述因果 |
| 跨会话仍成立的机制/约定 | `.wolf/cerebrum.md` 的 `## Key Learnings` | 引擎行为、项目约定、易错 API 这类**读代码不容易发现**的结论 |
| 架构级取舍与理由 | `.wolf/cerebrum.md` 的 `## Decision Log` | 一条决策一行到三行，写清"选了什么 + 为什么" |
| 会话流水 | `.wolf/memory.md` | 每个动作一行，追加即可 |
| **必须在每次会话都成立的强制约束** | **`CLAUDE.md`**（跨成员、入库）或 **`CLAUDE.LOCAL.md`**（本机专属、不入 git） | 简明确切、可执行；这两个文件会被 harness **每轮自动注入**，放这里的约束才真正"强制" |

要点：

- 重要约束**优先写 `CLAUDE.md`/`CLAUDE.LOCAL.md`**，而不是塞进 `.wolf/cerebrum.md`——
  后者的生效依赖"AI 主动去读"，前者是自动注入。
- `cerebrum.md` 的作用是**索引与提炼**，不是事件日志：同一个 bug 不要在两处各写一遍因果。
- `.wolf/cerebrum.md` 与 `.wolf/anatomy.md` 的结构分别被 `.wolf/hooks/pre-write.js`
  和 `.wolf/hooks/post-write.js` 按固定格式解析，**改标题/改格式前先看 hook 实现**。

## 强制约束

1. **commit 消息禁止添加 `Co-Authored-By` 尾注。**
2. **commit 消息必须详细描述修改内容。**
3. **禁止使用 `UE_LOG` 打印日志**，一律使用 `Source/Wildforge/Public/Utils/WildforgeLog.h` 里的宏：
   - 等级：`WFLOG_INFO` / `WFLOG_WARNING` / `WFLOG_ERROR`；蓝图侧调同名静态函数 `UWildforgeLog::Info/Warning/Error`。
   - 它们是 `printf` 风格、需要 `TEXT()` 的**格式字符串字面量**（内部已做 `FString::Printf(TEXT(Format), ...)`），
     所以写 `WFLOG_WARNING("ResizeContainer: 新容量 %d 小于已用数量 %d", NewCapacity, UsedCount)`，
     **不要**再套 `TEXT(...)`，也**不要**用 `{}` 占位符（`FString::Format` 那套不适用）。
   - 原因：`UE_LOG(LogTemp, ...)` 只进终端/Output Log，不落盘。`WFLOG_*` 经 `LogWildforge` 同时输出到终端
     与 `Saved/Logs/Wildforge/<日期>/<等级>.log`（异步写、跨日切目录），便于事后排查联机问题。
   - 唯一例外：`Utils/WildforgeLog.cpp` 自身内部的 `UE_LOG(LogWildforge, ...)`（它就是落盘设备的上游）。
   - 新增 include：`#include "Utils/WildforgeLog.h"`。
4. **客户端不得直接调用 `BlueprintAuthorityOnly` 的权威函数**（`UItemContainer` 的
   `InitializeContainer` / `AddItemStack` / `AddItemByID` / `RemoveItem` / `RemoveAllItem` / `SwapSlots` /
   `MoveOrMergeItem` / `ClearContainer` / `ResizeContainer` / `OrganizeContainer`，`UCharacterAttributes`
   的 `SetHealth` / `AddHealth` / `SetMaxHealth`）。它们在非权威端的 callspace 是 `Absorbed`，
   调用会被引擎**静默丢弃**——单机/监听服务器上看不出问题，联机客户端上直接失效。
   - UI / 输入 / 蓝图等**可能在客户端执行**的代码：一律调对应的 `Server_*` RPC
     （如 `Server_MoveOrMerge`、`Server_OrganizeContainer`）。Server RPC 在服务器上会**就地同步执行**，
     所以单机与联机走同一条路径，**不要**写 `HasAuthority() ? 本地调用 : 发 RPC` 的分支。
   - 纯服务器流程（掉落生成、合成产物、存档恢复等）可直接调权威函数。
   - **禁止把 `FItemInformation` 从客户端发给服务器**：RPC 只传 `ItemID` + 数量或槽位索引，服务器负责查表与校验。
   - **在 `UActorComponent` 子类里做权威判断，必须用 `Utils/WildforgeAuthority.h` 的
     `IsAuthoritativeForActorComponent(this)`，不要写 `HasAuthority()`**——后者是 `AActor` 的方法，
     组件上不存在该标识符，写了会直接编译失败（`error C3861: "HasAuthority": 找不到标识符`）。
     其实现即 `GetOwner()->HasAuthority()`（含空指针保护）。
   - 新增权威修改函数时，照抄 `UItemContainer.cpp` 顶部 `WF_CONTAINER_AUTHORITY_GUARD(RetVal)` 的门禁模式
     （非权威端记一条 `WFLOG_ERROR` 并安全返回），让同类误用立刻可见而不是静默失效。
