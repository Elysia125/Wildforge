# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-10-24T17:15:56.908Z
> Files: 137 tracked | Anatomy hits: 0 | Misses: 0

## ./

- `.clang-format` — 基于 Unreal Engine 风格的配置 (~116 tok)
- `.clangd` — UBT 的 -Mode=GenerateClangDatabase 以 clang-cl（CL 驱动）生成 compile_commands.json： (~101 tok)
- `.gitignore` — Git ignore rules（排除体积过大的第三方资源包 `Content/ProceduralNaturePack/`，本机专属文件 CLAUDE.LOCAL.md 不入库） (~84 tok)
- `.ignore` (~30 tok)
- `CLAUDE.local.md` — CLAUDE.LOCAL.md (~1605 tok)
- `CLAUDE.LOCAL.md` — CLAUDE.LOCAL.md (~1424 tok)
- `CLAUDE.md` — CLAUDE.md (~2863 tok)
- `README.md` — Project documentation (~11 tok)
- `Wildforge.code-workspace` (~4460 tok)
- `Wildforge.uproject` (~124 tok)

## C:/Users/xf317/AppData/Local/Temp/

- `wf_capture_parent.py` — p, field (~539 tok)
- `wf_clangd_info.ps1` (~82 tok)
- `wf_dump_current.py` — log, get, call, describe_slot (~1064 tok)
- `wf_dump_inventory.py` — log, get, call, describe_slot (~753 tok)
- `wf_dump_inventory2.py` — log, get, call, describe_slot (~860 tok)
- `wf_dump_panels.py` — log, get, call, dump (~565 tok)
- `wf_dump_ui.py` — p, try_prop, dump_widget (~917 tok)
- `wf_dump_widget.py` — get, dump_widget (~859 tok)
- `wf_dump_widget2.py` — object: get, dump_widget (~982 tok)
- `wf_dump_widget4.py` — object: log, get, dump_widget (~1022 tok)
- `wf_dump_widget5.py` — log, get, dump (~1043 tok)
- `wf_find_tree.py` — log, get (~444 tok)
- `wf_fix_clangd.js` — Declares fs (~96 tok)
- `wf_introspect.py` — log (~343 tok)
- `wf_introspect2.py` — p (~847 tok)
- `wf_subobjects.py` — p, try_prop (~1206 tok)
- `wf_syntax_check.ps1` — Declares Invoke (~661 tok)
- `wf_tree_full.py` — log, get, call, describe_slot (~1088 tok)
- `wf_tree.py` — p, field, walk (~635 tok)
- `wf_widgets.py` — p, field (~906 tok)
- `wf_wolf_record.js` — fs: eolOf (~992 tok)
- `wf-crawl-check-msvc.ps1` — 单 TU 真编译级检查（cl.exe /Zs 语法检查），用 UBT 自己生成的 MSVC 形态 obj.rsp + 真实的 MSVC PCH。 (~980 tok)
- `wf-crawl-check.ps1` — 单 TU 真编译级检查（clang-cl -fsyntax-only），针对本次 UBT 生成的 MSVC 形态 obj.rsp。 (~835 tok)
- `wf-probe-attack.ps1` (~384 tok)
- `wf-syntax-attack.ps1` (~167 tok)
- `wf-syntax-check.ps1` (~252 tok)
- `wf-syntax-check2.ps1` (~191 tok)

## C:/Users/xf317/AppData/Local/Temp/wfparse/

- `find_array2.py` — rd_fstring, parse (~500 tok)

## Config/

- `DefaultEditor.ini` (~14878 tok)
- `DefaultEditorPerProjectUserSettings.ini` (~19 tok)
- `DefaultEngine.ini` (~942 tok)
- `DefaultGame.ini` (~333 tok)
- `DefaultInput.ini` (~3677 tok)

## Plugins/Plugins/UmgDesignSystem/

- `UmgDesignSystem.uplugin` (~315 tok)

## Source/Wildforge/

- `Wildforge.Build.cs` — 模块构建规则：PublicDependencyModuleNames = Core, CoreUObject, Engine, InputCore, UMG, Slate, SlateCore, DeveloperSettings, AnimationModifiers, AnimationBlueprintLibrary（Enhanced Input 未列出，靠引擎传递依赖） (~500 tok)
- `Wildforge.cpp` — FWildforgeModule（FDefaultGameModuleImpl 子类）：Startup/ShutdownModule 注册 UWildforgeLog 文件输出设备 (~400 tok)
- `Wildforge.h` — 模块声明头（PrimaryGameModuleHeader） (~100 tok)

## Source/Wildforge/Private/Character/

- `BaseCharacter.cpp` — ABaseCharacter 实现：构造函数只做两件事（bReplicates=true + 创建 UCharacterAttributes），BeginPlay/Tick/SetupPlayerInputComponent 均为空壳并各留一条生命周期日志。**加速与闪现已整体搬迁到 USprintBoostComponent / UBlinkComponent**，本文件不再有任何速度/位移逻辑 (~250 tok)

## Source/Wildforge/Private/Character/Adaptor/

- `CharacterAdaptor.cpp` — GetDebugString 实现：把网格体/动画蓝图/各组件蒙太奇的配置情况拼成一行日志快照（空资源显示「未配置」） (~400 tok)

## Source/Wildforge/Private/Character/AnimNotify/

- `AnimNotifyAttackHit.cpp` — 命中帧通知实现：判 `MeshComp->GetOwner()->HasAuthority()` 才调 PerformDamageTrace（表现层特效各端本地调 PlayAttackEffects） (~250 tok)
- `AnimNotifyCombo.cpp` — 连击窗口通知实现：只发 `Server_NotifyComboWindow()`（不再直接写 bCanCombo——那是玩法状态，客户端写了无效） (~200 tok)

## Source/Wildforge/Private/Character/Components/

- `AttackComponent.cpp` — 攻击组件实现：服务器权威攻击链 + Multicast 蒙太奇（携带资产+Section）；武器招式表走 `TSubclassOf<UWeaponAttackProfile>` 的 CDO（Set/Clear/OnRep）；ini 覆盖/伤害轨迹/移动锁 (~9800 tok)
- `BlinkComponent.cpp` — 闪现实现：Sweep + 落点校验全在权威端；**新增 BeginPlay**（唯一职责是应用 ini 覆盖），4 项调参 COND_InitialOnly（含原先根本没复制的 BlinkCooldown） (~5300 tok)
- `CharacterAttributes.cpp` — 属性组件实现：权威端 ini 覆盖、等级曲线、精力自然回复 Tick（最近消耗后延迟 EnergyRegenDelay 秒，再按 EnergyRegenPerSecond 回复） (~4000 tok)
- `CrawlingComponent.cpp` — 趴下实现：姿态/过渡状态机 + 速度镜像（一律经 SprintBoostComponent::SetBaseMaxSpeed）；BeginPlay 先应用 5 项 ini 覆盖、再订阅动画回调 (~10500 tok)
- `LandRollComponent.cpp` — Fill out your copyright notice in the Description page of Project Settings. (~7391 tok)
- `SlideComponent.cpp` — 滑行实现：速度曲线两端各跑一份（所以调参必须复制）；BeginPlay 先应用 14 项 ini 覆盖，全部 COND_InitialOnly (~10000 tok)
- `SprintBoostComponent.cpp` — Fill out your copyright notice in the Description page of Project Settings. (~11798 tok)

## Source/Wildforge/Private/Character/Player/

- `PlayerCharacter.cpp` — 实现：构造函数装配 Inventory/QuickContainer/ContainerInteractionComponent 与各能力组件；BeginPlay 应用适配器、订阅移动门控事件、权威端读 PlayerCharacterSettings 初始化背包/快捷栏；PostNetInit 打印客户端生效配置；移动锁按来源集合记账（软锁不碰移动组件，硬锁只留给定身） (~9500 tok)

## Source/Wildforge/Private/Core/

- `WildforgeGameMode.cpp` — GameMode 实现 (~200 tok)
- `WildforgePlayerController.cpp` — 控制器实现：主界面 Widget 创建与显示/隐藏切换 (~700 tok)

## Source/Wildforge/Private/ItemSystem/Actors/

- `ItemMaster.cpp` — AItemMaster 实现（空） (~80 tok)

## Source/Wildforge/Private/ItemSystem/Components/

- `ContainerInteractionComponent.cpp` — 交互组件实现：ValidateTransferRequest 统一校验 PC/Pawn/权限/距离；Server_RequestOpenContainer 对宝箱调 OpenChestOnServer（避免 OpenChest 递归）后写 ActiveContainer；拥有者端创建宝箱窗口并 ApplyWindowLayout 居中定尺寸、切 FInputModeUIOnly；销毁窗口时按背包状态恢复 GameAndUI 或 GameOnly；含跨容器移动与全体转移 RPC (~5200 tok)
- `ItemContainer.cpp` — 容器核心实现：复制数组 + 派生索引重建；`AddItemStackInternal`（先并入同 ItemID 未满堆、再填空槽，不广播）供 `AddItemStack` 与 `TransferAllToContainer` 复用；`TransferAllToContainer` 快照源槽逐堆转移、部分装不下留在源容器、每个容器只广播一次；全部修改函数走 `WF_CONTAINER_AUTHORITY_GUARD` (~11000 tok)
- `PlayerInventory.cpp` — UPlayerInventory 实现（固定容器类型为 PlayerInventory） (~100 tok)
- `QuickContainer.cpp` — UQuickContainer 实现：遍历快捷键配置，在拥有者 Actor 上按函数名动态绑定输入按键（FInputActionHandlerDynamicSignature），使用 WFLOG_* 记录配置、绑定失败与成功 (~404 tok)
- `StorageContainer.cpp` — include "ItemSystem/Components/StorageContainer.h" (~78 tok)

## Source/Wildforge/Private/ItemSystem/Database/

- `ItemDatabaseSubsystem.cpp` — 实现：Initialize 从 UItemSystemSettings 同步加载 DT_Items；EnsureIndex/BuildIndex 按「行内 ItemID，0 则回退行名数字」建 ItemID→定义索引（重复取先出现者，行结构非 FItemInformation 记 ERROR）；GetItemDefinition/FindItem O(1) 查找；GetAllItemIDs 供宝箱等服务器流程遍历筛选 (~900 tok)
- `ItemSystemSettings.cpp` — UItemSystemSettings 实现（仅包含头，无额外逻辑） (~60 tok)

## Source/Wildforge/Private/ItemSystem/UI/

- `InventorySlotDragDropOperation.cpp` — 拖拽操作实现：RestoreSourceWidget() 经弱引用 Pin 到源控件后 SetRenderOpacity(1.f)（失效即跳过） (~150 tok)
- `InventorySlotWidget.cpp` — 单格控件实现：状态重放（Slate 重建安全）、每格按 (容器, 索引) 拉取并跳过未变数据；拖拽起点/落点做 HasAccess 预检，落点经 UContainerInteractionComponent::Server_MoveItemBetweenContainers 统一走服务器（同容器也走），无组件时同容器回退 Server_MoveOrMerge；悬停只查权限不查距离以免刷日志 (~6200 tok)
- `InventoryUserWidget.cpp` — include "ItemSystem/UI/InventoryUserWidget.h" (~1914 tok)
- `ItemContainerGrid.cpp` — 网格实现：InitializeGrid 里 `InSlotsPerRow <= 0` 表示沿用控件自己的 SlotsPerRow（WBP 设计器里配的列数，调用方硬编码会把它冲掉），统一夹到 >= 1（LayoutSlots 拿它当除数）；NativeConstruct **无条件**以拥有者 Pawn 的实时查找结果重解析容器（不再 `if(!Container)` 跳过——非空≠有效；对继承的失效弱引用记警告，见 bug-064）；HandleContainerChanged 只做结构（容量变化才 EnsureSlotCount+LayoutSlots）；RefreshGrid 为全量入口；EnsureSlotCount 目标数 [0,512] 夹紧后补建时当场 InitializeSlot、移除时先 UnbindFromContainer (~1800 tok)
- `QuickBarWidget.cpp` — 快捷栏：InitializeGrid（绑快捷键，**不依赖**槽位控件是否已建出）+ HandleContainerChanged 重写（容量到位时把 SlotsPerRow 校正为容量＝单行，再补按键标签）+ RefreshHotkeyLabels（文本没变不写）+ SetupHotkeyBindings/HandleHotkeyTriggered（Enhanced Input 动态建 Action 并在 Owner 上 FindFunction） (~2900 tok)

## Source/Wildforge/Private/UI/

- `MainUserWidget.cpp` — Fill out your copyright notice in the Description page of Project Settings. (~3318 tok)
- `ToastWidget.cpp` — Toast 状态机：排队与淡入/停留/淡出、相同文本合并（正在显示则重置停留，队尾相同则替换）、蓝图钩子与 Slate 重建重放。 (~3000 tok)
- `WildforgeUIManager.cpp` — 管理器实现：视口界面懒创建/移除、CanShow 判定求值、Toast 懒创建与警告样式（ShowToastWarning 红字深红底）。 (~6200 tok)

## Source/Wildforge/Private/Utils/

- `WildforgeLog.cpp` — 实现：FWildforgeLogDevice（FOutputDevice）按等级分派到 3 个 FLevelFileWriter（各含 FAsyncWriter 后台线程 + 独立锁），UTF-8 无 BOM，跨日切目录；仅此处允许内部 UE_LOG(LogWildforge, …) (~1600 tok)

## Source/Wildforge/Private/World/

- `ChestActor.cpp` — 实现：构造建 UStorageContainer + bReplicates；服务器 BeginPlay 读 UChestSettings → 初始化容器 → GenerateLoot（等级稀有度池 + Fisher-Yates；每堆数量按 LootStackMin~LootStackMax 随机并按物品 MaxStackSize / IsStackable 夹紧）；OpenChest 转发交互组件；OpenChestOnServer 校验权限 / 窗口类并维护 ActiveUsers；SetOpenedState / OnRep 驱动 BP_OnChestOpened/Closed。 (~4600 tok)

## Source/Wildforge/Private/World/UI/

- `ChestWindowWidget.cpp` — 宝箱窗口实现：InitializeChestWindow 注入宝箱/容器并绑定宝箱与背包两个网格，关闭/全体转移经玩家交互组件发 RPC；ApplyWindowLayout 按 WindowSize 固定尺寸居中（先设尺寸/位置、最后设锚点与对齐，见 GameViewportSubsystem）；NativeDestruct 兜底发关闭请求 (~2600 tok)

## Source/Wildforge/Public/Character/

- `BaseCharacter.h` — ABaseCharacter（ACharacter 子类）：只负责「所有角色都存在」的两件事——bReplicates=true + 持有 UCharacterAttributes；**加速与闪现已拆成独立组件挂到 APlayerCharacter**（本类不再有任何速度/位移 API，旧蓝图节点需重连 GetSprintBoostComponent/GetBlinkComponent） (~800 tok)

## Source/Wildforge/Public/Character/Adaptor/

- `CharacterAdaptor.h` — UCharacterAdaptor（UDataAsset，BlueprintType/Blueprintable）：**一个角色一份**的资源集合——骨骼网格体/动画蓝图/可选网格相对变换/默认（徒手）攻击蒙太奇列表/翻滚/滑行/加速/闪现/趴下的蒙太奇与 Section；在角色蓝图类默认值里以 TSubclassOf 指定适配器蓝图类，APlayerCharacter 每端读 CDO 应用；网格与 AnimClass 留空=保留蓝图默认值，蒙太奇留空=明确清空；只放资源不放可调数值 (~1500 tok)

## Source/Wildforge/Public/Character/AnimNotify/

- `AnimNotifyAttackHit.h` — UAnimNotify_AttackHit：命中帧通知（实现在 .cpp 里判权威端后调 PerformDamageTrace，各端本地调 PlayAttackEffects） (~120 tok)
- `AnimNotifyCombo.h` — UAnimNotify_Combo：连击窗口通知（实现在 .cpp 里发 Server_NotifyComboWindow，不再直接写 bCanCombo） (~120 tok)

## Source/Wildforge/Public/Character/Components/

- `AttackComponent.h` — 攻击组件（服务器权威）。 (~4575 tok)
- `BlinkComponent.h` — 点按闪现组件（服务器权威）：距离/冷却/播速带 Replicated（COND_InitialOnly）；新增 protected BeginPlay + ApplyGameplaySettingsOverrides（本组件原先没有 BeginPlay） (~2500 tok)
- `CharacterAttributes.h` — 角色的基础属性：生命、精力（含权威端自然回复参数 EnergyRegenPerSecond / EnergyRegenDelay 与最近消耗时间）、等级与经验。 (~4000 tok)
- `CrawlingComponent.h` — 趴下（爬行）组件（服务器权威）：爬行速度/奔跑阈值/播速带 Replicated + ini 覆盖；速度改写一律经 USprintBoostComponent::SetBaseMaxSpeed (~5100 tok)
- `LandRollComponent.h` — 翻滚组件（服务器权威）。 (~3702 tok)
- `SlideComponent.h` — 滑行（低姿态冲刺滑铲）组件（服务器权威）：14 项调参带 Replicated（COND_InitialOnly）+ ini 覆盖；两端各跑同一份速度曲线所以必须复制 (~5600 tok)
- `SprintBoostComponent.h` — 加速（长按冲刺）组件 —— **服务器权威**。 (~6950 tok)
- `structs/AttackMontageData.h` — FAttackMontageData：Montage + SectionNames（TArray<FName>，默认 {NAME_None}），攻击蒙太奇分段数据 (~150 tok)
- `WeaponAttackProfile.h` — 武器攻击配置：**一件武器一份**的招式表；做成蓝图子类（Class Reference/TSubclassOf，数据读 CDO），`AttackMontageList` 非空时覆盖角色默认表 (~550 tok)

## Source/Wildforge/Public/Character/Player/

- `PlayerCharacter.h` — 玩家角色装配点：背包/快捷栏/容器交互组件 + 攻击/翻滚/加速/滑行/趴下组件；移动锁按来源集合记账（TSet + 硬锁子集），IsMoveInputIgnored 实现攻击输入锁；适配器 TSubclassOf 复制到所有端；GetContainerInteractionComponent 是宝箱打开/跨容器拖拽入口 (~5000 tok)

## Source/Wildforge/Public/Character/Settings/

- `AttackComponentSettings.h` — `UAttackComponentSettings`：冷却/连击间隔/连击融合时间/伤害/范围/扫描半径/扫描高度偏移，8 对 bOverride_* (~1200 tok)
- `BlinkComponentSettings.h` — `UBlinkComponentSettings`：最大距离/冷却/是否保留速度/蒙太奇播速，4 对 (~700 tok)
- `CharacterAttributesSettings.h` — 角色属性组件（`UCharacterAttributes`）的可调数值：初始等级、生命/精力上限、 (~1514 tok)
- `CrawlingComponentSettings.h` — `UCrawlingComponentSettings`：趴下移速（ClampMin 1，见 bug-027）/奔跑阈值/冷却/BlendOut 收尾/播速，5 对 (~800 tok)
- `LandRollComponentSettings.h` — 翻滚组件（`ULandRollComponent`）的可调数值。 (~799 tok)
- `PlayerCharacterSettings.h` — `UPlayerCharacterSettings`：**角色自身**（非组件）的调参，当前只有背包容量 InventoryCapacity（ClampMin 1） (~400 tok)
- `SlideComponentSettings.h` — `USlideComponentSettings`：时长/起止速度/减速速率/续滑门限与宽限/是否可转向/离地结束/地面摩擦/刹车减速/蒙太奇播速与 BlendOut，14 对，按 Slide|Speed / Rules / Steering / Anim 分组 (~1800 tok)
- `SprintBoostComponentSettings.h` — 加速组件（`USprintBoostComponent`）的可调数值。 (~839 tok)

## Source/Wildforge/Public/Core/

- `WildforgeGameMode.h` — AWildforgeGameMode (~200 tok)
- `WildforgePlayerController.h` — AWildforgePlayerController：ShowInventory()/HideInventory 切换 FInputModeGameAndUI ↔ GameOnly（光标显隐） (~500 tok)

## Source/Wildforge/Public/ItemSystem/Actors/

- `ItemMaster.h` — AItemMaster：物品 Actor 桩（暂空），被 FItemInformation::ItemClass 引用 (~200 tok)

## Source/Wildforge/Public/ItemSystem/Components/

- `ContainerInteractionComponent.h` — 容器交互组件声明：ActiveContainer（COND_OwnerOnly）与交互距离（COND_InitialOnly）复制；打开/关闭宝箱、跨容器移动、全体转移 Server RPC；宝箱窗口生命周期与输入模式恢复的说明 (~2200 tok)
- `ItemContainer.h` — 容器组件声明：复制 `Slots` / `SlotOccupied` + 派生索引；增删改查/整理/调整大小均为 `BlueprintAuthorityOnly`，另有 `Server_*` RPC 兜底；权限模式 `HasAccess` / `IsOwnedBy`；新增 `TransferAllToContainer`（全体转移）与私有 `AddItemStackInternal`（无广播原语） (~3800 tok)
- `PlayerInventory.h` — UPlayerInventory：UItemContainer 子类，构造时固定容器类型为 PlayerInventory；内容复制条件继承基类 COND_OwnerOnly (~300 tok)
- `StorageContainer.h` — 世界容器组件（UItemContainer 子类，构造锁定 WorldStorage + 内容复制覆写为 COND_None）；**保持通用**：宝箱等级 / 战利品 / 开箱表现都在 AChestActor，不要往这里加宝箱专属字段 (~520 tok)

## Source/Wildforge/Public/ItemSystem/Database/

- `ItemDatabaseSubsystem.h` — 物品定义数据库（单一真相源）：Get/FindItem/GetItemDefinition/GetItemTable/GetAllItemIDs（遍历全部 ItemID，供掉落表按稀有度筛选）；ItemIndex + CachedTable 懒构建；服务器与客户端都可用 (~600 tok)
- `ItemSystemSettings.h` — UDeveloperSettings（Project Settings > Game > Item System）：TSoftObjectPtr<UDataTable> ItemTable，配置驱动物品表 (~500 tok)

## Source/Wildforge/Public/ItemSystem/Enums/

- `ChestLevel.h` — EChestLevel（Common/Rare/Legendary，普通/稀有/传奇）；归属 AChestActor（不是 UStorageContainer）；枚举值进入复制与存档，只追加不重排 (~180 tok)
- `ContainerAccessMode.h` — EContainerAccessMode：OwnerOnly/Public/Whitelist/Locked；只回答「玩家 P 能否访问容器 C」，不负责内容可见性（那由复制条件决定） (~350 tok)
- `ContainerType.h` — EContainerType：PlayerInventory/PlayerHotbar/PlayerStorage/PlayerArmor + 末尾追加 WorldStorage（不改变已有枚举值） (~150 tok)
- `ItemArmor.h` — EItemArmor 枚举 (~150 tok)
- `ItemRarity.h` — EItemRarity 枚举（Common…） (~150 tok)
- `ItemType.h` — EItemType 枚举（Other/…） (~200 tok)

## Source/Wildforge/Public/ItemSystem/Settings/

- `ContainerInteractionSettings.h` — UContainerInteractionSettings（Project Settings > Game > 容器交互）：bOverride_MaxInteractionDistance + MaxInteractionDistance（ClampMin 0）；仅权威端读 ini，客户端只认 COND_InitialOnly 复制值 (~450 tok)

## Source/Wildforge/Public/ItemSystem/Structs/

- `HotkeyBinding.h` — FHotkeyBinding（BlueprintType）：快捷键配置三项——Chord（FInputChord，键+修饰键）、FunctionName（在 Owner Actor 蓝图上 FindFunction 的 UFUNCTION 名）、TriggerEvent（ETriggerEvent，默认 Started）；供 UQuickBarWidget::HotkeyBindings 与 UQuickContainer 共用 (~300 tok)
- `ItemInfo.h` — FItemInformation（FTableRowBase，需 include Engine/DataTable.h——别靠 SharedPCH，见 bug-014）：ItemID/ItemName/ItemDesc/ItemQuality（兼作堆叠数量）/ItemDamage/IsStackable/MaxStackSize/ItemCurHP/ItemMaxHP/ItemIcon/ItemType/ItemRarity/ItemArmor/ItemClass/UseAmmo/Ammo/AmmoMax (~900 tok)
- `QuickSlotContext.h` — FQuickSlotContext（BlueprintType）：快捷栏触发时传给蓝图函数的上下文——Item（FItemInformation，按值）+ SlotWidget（UInventorySlotWidget*，仅前向声明）；自身 include ItemInfo.h（2026-10-11 补齐：值成员需完整类型，原先靠包含方 include 顺序，单独解析即报 unknown type name） (~200 tok)

## Source/Wildforge/Public/ItemSystem/UI/

- `InventorySlotDragDropOperation.h` — 拖拽操作（UDragDropOperation）：SourceSlotIndex/SourceContainerType/ItemInfo/SourceContainer + TWeakObjectPtr<UUserWidget> SourceWidget（弱引用避免源控件被网格刷新销毁后悬空）与 RestoreSourceWidget() 兜底复位，供 NativeOnDrop 判定来源并恢复显示 (~700 tok)
- `InventorySlotWidget.h` — 单格控件：BindWidget 子控件 + 选中/高亮染色（基准色首次从设计器抓取，见 bug-015）；InitializeSlot/UnbindFromContainer/RefreshFromContainer 状态重放；拖拽 CanAcceptInventoryDrag（权限预检）与 GetOwningInteractionComponent；容器用 TWeakObjectPtr（bug-064） (~2700 tok)
- `InventoryUserWidget.h` — 背包主界面：FInventoryThemeConfig 主题结构体；InitializeInventory/ShowInventoryPage/ShowCraftPage；BindWidget 按钮/Tab/WidgetSwitcher/ItemContainerGrid/动画；主题美化 ApplyInitialThemeStyles；**ItemContainer 为 TWeakObjectPtr（使用点先 Get()，见 bug-064）** (~2200 tok)
- `ItemContainerGrid.h` — 网格基类：InitializeGrid / RefreshGrid / SetSlotsPerRow / GetContainer；HandleContainerChanged 为 **virtual**（子类可补结构变化后的收尾，如快捷栏按键标签）；容器用 TWeakObjectPtr (~860 tok)
- `QuickBarWidget.h` — 容器结构变化（容量到位 / 增删槽位）后的收尾：先把「每行个数」校正为容量 (~534 tok)

## Source/Wildforge/Public/UI/

- `MainUserWidget.h` — Fill out your copyright notice in the Description page of Project Settings. (~1222 tok)
- `ToastMessage.h` — FToastMessage（BlueprintType）：Message / TextColor / BackgroundColor / Icon + IconTint / Duration（<=0 回退控件默认时长）；全部只影响客户端表现 (~370 tok)
- `ToastWidget.h` — Toast 提示控件基类（必须由蓝图子类提供具体外观）。 (~1986 tok)
- `WildforgeUIManager.h` — 每本地玩家一份的界面管理器：按类管理视口界面 + 可绑定展示判定；Toast 入口 ShowToast / ShowToastStyled / ShowToastWarning / ShowToastMessage（警告样式红字深红底，默认 4 秒）。 (~2700 tok)

## Source/Wildforge/Public/UI/Settings/

- `ToastSettings.h` — UToastSettings（UDeveloperSettings）：ToastWidgetClass（TSoftClassPtr<UToastWidget>，指向 WBP 子类，首次弹提示时加载）+ ZOrder（默认 100，仅首次创建生效）；表现参数仍在 WBP 类默认值，不放进 ini (~450 tok)

## Source/Wildforge/Public/Utils/

- `ComponentAuthorityGuard.h` — **WF_COMPONENT_AUTHORITY_GUARD(ReturnValue)**：UActorComponent 子类通用的开发期权威门禁宏（非权威端 WFLOG_ERROR + `return ReturnValue`，void 用 `void()`），供 SprintBoost/Blink 等新组件统一使用；内含「组件里不能写 HasAuthority（C3861）」的说明。（曾存在的 `AuthorityGuard.h` / `WF_AUTHORITY_GUARD` 已被用户删除——与本文档功能重复、仓库内无任何 TU 引用，不要恢复） (~400 tok)
- `WildforgeAuthority.h` — 权威端判断辅助（inline）：IsAuthoritativeForActorComponent(UActorComponent*) = GetOwner()->HasAuthority()；供组件子类使用（HasAuthority 是 AActor 的方法，组件上没有，直接写会 C3861） (~350 tok)
- `WildforgeLog.h` — 日志工具（UBlueprintFunctionLibrary）：Info/Warning/Error/WFLOG_* 宏；注册接口 WildforgeLogging::RegisterFileOutput；文件输出走异步设备 (FAsyncWriter) (~1000 tok)

## Source/Wildforge/Public/World/

- `ChestActor.h` — 可交互宝箱：等级 + 世界容器 + 开箱表现 + 宝箱窗口；LootCountMin/Max 控制堆数，LootStackMin/Max 控制每堆数量（按物品 MaxStackSize 夹紧）。 (~2200 tok)

## Source/Wildforge/Public/World/Settings/

- `ChestSettings.h` — 宝箱（`AChestActor`）的可调数值：容量、战利品堆数、每堆数量；仅权威端读取。 (~1000 tok)

## Source/Wildforge/Public/World/UI/

- `ChestWindowWidget.h` — 宝箱窗口 C++ 基类（Abstract）：数据注入、关闭/全体转移请求、ApplyWindowLayout；WindowSize 在 WBP Class Defaults 调整；文档说明 AddToViewport 默认全屏与 UIOnly 输入策略 (~1800 tok)
