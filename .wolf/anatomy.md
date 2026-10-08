# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-10-08T14:19:44.921Z
> Files: 86 tracked | Anatomy hits: 0 | Misses: 0

## ./

- `.clang-format` — 基于 Unreal Engine 风格的配置 (~116 tok)
- `.clangd` — UBT 的 -Mode=GenerateClangDatabase 以 clang-cl（CL 驱动）生成 compile_commands.json： (~101 tok)
- `.gitignore` — Git ignore rules（排除体积过大的第三方资源包 `Content/ProceduralNaturePack/`，本机专属文件 CLAUDE.LOCAL.md 不入库） (~84 tok)
- `.ignore` (~30 tok)
- `CLAUDE.local.md` — CLAUDE.LOCAL.md (~1605 tok)
- `CLAUDE.LOCAL.md` — CLAUDE.LOCAL.md (~1424 tok)
- `CLAUDE.md` — CLAUDE.md (~2439 tok)
- `README.md` — Project documentation (~11 tok)
- `Wildforge.code-workspace` (~4460 tok)
- `Wildforge.uproject` (~124 tok)

## C:/Users/xf317/AppData/Local/Temp/

- `wf_clangd_info.ps1` (~82 tok)
- `wf_fix_clangd.js` — Declares fs (~96 tok)
- `wf_wolf_record.js` — fs: eolOf (~992 tok)
- `wf-crawl-check-msvc.ps1` — 单 TU 真编译级检查（cl.exe /Zs 语法检查），用 UBT 自己生成的 MSVC 形态 obj.rsp + 真实的 MSVC PCH。 (~980 tok)
- `wf-crawl-check.ps1` — 单 TU 真编译级检查（clang-cl -fsyntax-only），针对本次 UBT 生成的 MSVC 形态 obj.rsp。 (~835 tok)

## Config/

- `DefaultEditor.ini` (~14878 tok)
- `DefaultEditorPerProjectUserSettings.ini` (~19 tok)
- `DefaultEngine.ini` (~942 tok)
- `DefaultGame.ini` — 含 UItemSystemSettings 的 ItemTable 路径（Project Settings 序列化到此） (~36 tok)
- `DefaultInput.ini` (~3677 tok)

## Source/Wildforge/

- `Wildforge.Build.cs` — 模块构建规则：PublicDependencyModuleNames = Core, CoreUObject, Engine, InputCore, UMG, Slate, SlateCore, DeveloperSettings, AnimationModifiers, AnimationBlueprintLibrary（Enhanced Input 未列出，靠引擎传递依赖） (~500 tok)
- `Wildforge.cpp` — FWildforgeModule（FDefaultGameModuleImpl 子类）：Startup/ShutdownModule 注册 UWildforgeLog 文件输出设备 (~400 tok)
- `Wildforge.h` — 模块声明头（PrimaryGameModuleHeader） (~100 tok)

## Source/Wildforge/Private/Character/

- `BaseCharacter.cpp` — ABaseCharacter 实现：构造函数只做两件事（bReplicates=true + 创建 UCharacterAttributes），BeginPlay/Tick/SetupPlayerInputComponent 均为空壳并各留一条生命周期日志。**加速与闪现已整体搬迁到 USprintBoostComponent / UBlinkComponent**，本文件不再有任何速度/位移逻辑 (~250 tok)

## Source/Wildforge/Private/Character/AnimNotify/

- `AnimNotifyAttackHit.cpp` — 命中帧通知实现：判 `MeshComp->GetOwner()->HasAuthority()` 才调 PerformDamageTrace（表现层特效各端本地调 PlayAttackEffects） (~250 tok)
- `AnimNotifyCombo.cpp` — 连击窗口通知实现：只发 `Server_NotifyComboWindow()`（不再直接写 bCanCombo——那是玩法状态，客户端写了无效） (~200 tok)

## Source/Wildforge/Private/Character/Components/

- `AttackComponent.cpp` — 攻击组件实现：权威判定 + 蒙太奇分段连击 + 伤害扫描；BeginPlay 先调 ApplyGameplaySettingsOverrides（只有权威端读 ini），8 项调参走 COND_InitialOnly 复制 (~7600 tok)
- `BlinkComponent.cpp` — 闪现实现：Sweep + 落点校验全在权威端；**新增 BeginPlay**（唯一职责是应用 ini 覆盖），4 项调参 COND_InitialOnly（含原先根本没复制的 BlinkCooldown） (~5300 tok)
- `CharacterAttributes.cpp` — 属性组件实现：DOREPLIFETIME + OnRep 广播委托，SetHealth/SetMaxHealth 带 Clamp (~900 tok)
- `CrawlingComponent.cpp` — 趴下实现：姿态/过渡状态机 + 速度镜像（一律经 SprintBoostComponent::SetBaseMaxSpeed）；BeginPlay 先应用 5 项 ini 覆盖、再订阅动画回调 (~10500 tok)
- `LandRollComponent.cpp` — 翻滚实现：BeginPlay 先应用 4 项 ini 覆盖再订阅动画回调；调参走 COND_InitialOnly（原 LandRollCooldown 沿用 COND_OwnerOnly） (~6900 tok)
- `SlideComponent.cpp` — 滑行实现：速度曲线两端各跑一份（所以调参必须复制）；BeginPlay 先应用 14 项 ini 覆盖，全部 COND_InitialOnly (~10000 tok)
- `SprintBoostComponent.cpp` — 加速实现：独占 MaxWalkSpeed 快照/还原 + 镜像给拥有者客户端；BeginPlay 先应用 4 项 ini 覆盖 (~10900 tok)

## Source/Wildforge/Private/Character/Player/

- `PlayerCharacter.cpp` — 玩家角色实现：装配 6 个能力组件 + 移动门控订阅；BeginPlay 解析背包容量（ini 覆盖 > 蓝图类默认值 > 30）后 InitializeContainer；**新增 PostNetInit**（客户端收到出生束后打印各组件生效值，与服务端 [配置] 日志对照） (~6200 tok)

## Source/Wildforge/Private/Core/

- `WildforgeGameMode.cpp` — GameMode 实现 (~200 tok)
- `WildforgePlayerController.cpp` — 控制器实现：主界面 Widget 创建与显示/隐藏切换 (~700 tok)

## Source/Wildforge/Private/ItemSystem/Actors/

- `ItemMaster.cpp` — AItemMaster 实现（空） (~80 tok)

## Source/Wildforge/Private/ItemSystem/Components/

- `ItemContainer.cpp` — 容器实现：RebuildDerivedState 由复制数据重建索引；AddItemStack 按堆叠放置；RemoveSlotQuantityInternal(单槽)/RemoveByItemIDInternal(跨槽)按数量逐格扣除；MoveOrMergeItem 合并/交换（合并只在源与目标两槽间重分配数量：目标加满、余数留源，源清空则回收槽位）；OrganizeContainer 合并/排序；顶部 WF_CONTAINER_AUTHORITY_GUARD 宏给 10 个修改函数加非权威端门禁；日志统一 WFLOG_*；Server_* RPC 转调权威方法 (~4000 tok)
- `PlayerInventory.cpp` — UPlayerInventory 实现（固定容器类型为 PlayerInventory） (~100 tok)

## Source/Wildforge/Private/ItemSystem/Database/

- `ItemDatabaseSubsystem.cpp` — 子系统实现：Initialize 从 UItemSystemSettings 取 ItemTable 并 LoadSynchronous，GetRowMap 构建 int 键缓存；日志走 WFLOG_* (~1200 tok)
- `ItemSystemSettings.cpp` — UItemSystemSettings 实现（仅包含头，无额外逻辑） (~60 tok)

## Source/Wildforge/Private/ItemSystem/UI/

- `InventorySlotDragDropOperation.cpp` — 拖拽操作实现：RestoreSourceWidget() 经弱引用 Pin 到源控件后 SetRenderOpacity(1.f)（失效即跳过） (~150 tok)
- `InventorySlotWidget.cpp` — 单格实现：InitializeSlot 绑定 (容器,索引) 并退订/重订 + 首次拉取，HandleContainerChanged→RefreshFromContainer（GetItemAtSlot 拉取；本格没变直接返回，空槽不为空才 ClearSlot），UnbindFromContainer 在换绑/NativeDestruct 时退订；显示数据是控件状态（CurrentItem/bHasItemData），RefreshFromState() 是「状态 → 外观」唯一出口（NativeConstruct 也调它，构造/重建都能重放，别再手工 TakeWidget）；SetItemData 只写状态+重画（不要写数量早退，会连图标一起跳过）、ClearSlot 清状态+重画；RefreshSlotStyleColor 首次执行时懒抓设计器 BrushColor 作 NormalSlotColor，未高亮时还原它（别再硬编码默认色，见 bug-015）；NativeOnDragDetected 视觉类为空/指向原生类时退回 GetClass()、只 CreateWidget+InitializeSlot，创建 UInventorySlotDragDropOperation（回填 SourceContainer/SourceSlotIndex/SourceWidget）并半透明；NativeOnDrop 复位源控件显示（SetRenderOpacity，只复位透明度，着色由 Enter/Leave 平衡）→ 同槽/跨容器早退 → 发 Server_MoveOrMerge；DragOver 只接受本系统拖拽、Enter/Leave 高亮（SetHighlight_Implementation 染色） (~2800 tok)
- `InventoryUserWidget.cpp` — 背包主界面实现：NativeConstruct 取容器 + InitializeInventory（调 `InitializeGrid(容器)`——**每行个数不再硬编码 5**，改用网格控件自己的 SlotsPerRow）；Tab 切页 + 高亮/反馈动画；整理按钮走 Server_OrganizeContainer（RPC）；ApplyInitialThemeStyles 应用主题配色与阴影 (~1724 tok)
- `ItemContainerGrid.cpp` — 网格实现：InitializeGrid 里 `InSlotsPerRow <= 0` 表示沿用控件自己的 SlotsPerRow（WBP 设计器里配的列数，调用方硬编码会把它冲掉），统一夹到 >= 1（LayoutSlots 拿它当除数）；NativeConstruct 从拥有者 Pawn 找容器；HandleContainerChanged 只做结构（容量变化才 EnsureSlotCount+LayoutSlots）；RefreshGrid 为全量入口；EnsureSlotCount 补建时当场 InitializeSlot、移除时先 UnbindFromContainer (~1226 tok)

## Source/Wildforge/Private/UI/

- `MainUserWidget.cpp` — 主界面 Widget 实现 (~300 tok)

## Source/Wildforge/Private/Utils/

- `WildforgeLog.cpp` — 实现：FWildforgeLogDevice（FOutputDevice）按等级分派到 3 个 FLevelFileWriter（各含 FAsyncWriter 后台线程 + 独立锁），UTF-8 无 BOM，跨日切目录；仅此处允许内部 UE_LOG(LogWildforge, …) (~1600 tok)

## Source/Wildforge/Public/Character/

- `BaseCharacter.h` — ABaseCharacter（ACharacter 子类）：只负责「所有角色都存在」的两件事——bReplicates=true + 持有 UCharacterAttributes；**加速与闪现已拆成独立组件挂到 APlayerCharacter**（本类不再有任何速度/位移 API，旧蓝图节点需重连 GetSprintBoostComponent/GetBlinkComponent） (~800 tok)

## Source/Wildforge/Public/Character/AnimNotify/

- `AnimNotifyAttackHit.h` — UAnimNotify_AttackHit：命中帧通知（实现在 .cpp 里判权威端后调 PerformDamageTrace，各端本地调 PlayAttackEffects） (~120 tok)
- `AnimNotifyCombo.h` — UAnimNotify_Combo：连击窗口通知（实现在 .cpp 里发 Server_NotifyComboWindow，不再直接写 bCanCombo） (~120 tok)

## Source/Wildforge/Public/Character/Components/

- `AttackComponent.h` — 攻击组件（服务器权威）：连击间隔/伤害/扫描参数带 Replicated，可被 UAttackComponentSettings 的 ini 覆盖（服务器读 → COND_InitialOnly 复制） (~3700 tok)
- `BlinkComponent.h` — 点按闪现组件（服务器权威）：距离/冷却/播速带 Replicated（COND_InitialOnly）；新增 protected BeginPlay + ApplyGameplaySettingsOverrides（本组件原先没有 BeginPlay） (~2500 tok)
- `CharacterAttributes.h` — UCharacterAttributes：复制 Health/MaxHealth（ReplicatedUsing OnRep 广播 FOnHealthChanged/FOnMaxHealthChanged），Setter 为 BlueprintAuthorityOnly，且 SetHealth/SetMaxHealth 内联加非权威端门禁（IsAuthoritativeForActorComponent + WFLOG_ERROR） (~900 tok)
- `CrawlingComponent.h` — 趴下（爬行）组件（服务器权威）：爬行速度/奔跑阈值/播速带 Replicated + ini 覆盖；速度改写一律经 USprintBoostComponent::SetBaseMaxSpeed (~5100 tok)
- `LandRollComponent.h` — 翻滚组件（服务器权威）：冷却/播速/BlendOut 解锁/起手清零速度可被 ini 覆盖（冷却沿用 COND_OwnerOnly，其余 COND_InitialOnly） (~3600 tok)
- `SlideComponent.h` — 滑行（低姿态冲刺滑铲）组件（服务器权威）：14 项调参带 Replicated（COND_InitialOnly）+ ini 覆盖；两端各跑同一份速度曲线所以必须复制 (~5600 tok)
- `SprintBoostComponent.h` — 加速（长按冲刺）组件（服务器权威）：时长/方向门控/夹角/播速带 Replicated + ini 覆盖；MaxWalkSpeed 由本组件独占 (~6900 tok)
- `structs/AttackMontageData.h` — FAttackMontageData：Montage + SectionNames（TArray<FName>，默认 {NAME_None}），攻击蒙太奇分段数据 (~150 tok)

## Source/Wildforge/Public/Character/Player/

- `PlayerCharacter.h` — 玩家角色：把玩家专用的「能力组件」+ 背包装配在一起；InventoryCapacity 只是基线（ini 可覆盖），新增 PostNetInit 覆写（只读日志，验证调参复制链路） (~3700 tok)

## Source/Wildforge/Public/Character/Settings/

- `AttackComponentSettings.h` — `UAttackComponentSettings`：冷却/连击间隔/连击融合时间/伤害/范围/扫描半径/扫描高度偏移，8 对 bOverride_* (~1200 tok)
- `BlinkComponentSettings.h` — `UBlinkComponentSettings`：最大距离/冷却/是否保留速度/蒙太奇播速，4 对 (~700 tok)
- `CrawlingComponentSettings.h` — `UCrawlingComponentSettings`：趴下移速（ClampMin 1，见 bug-027）/奔跑阈值/冷却/BlendOut 收尾/播速，5 对 (~800 tok)
- `LandRollComponentSettings.h` — `ULandRollComponentSettings`：冷却/BlendOut 提前解锁/起手清零速度/播速，4 对 (~700 tok)
- `PlayerCharacterSettings.h` — `UPlayerCharacterSettings`：**角色自身**（非组件）的调参，当前只有背包容量 InventoryCapacity（ClampMin 1） (~400 tok)
- `SlideComponentSettings.h` — `USlideComponentSettings`：时长/起止速度/减速速率/续滑门限与宽限/是否可转向/离地结束/地面摩擦/刹车减速/蒙太奇播速与 BlendOut，14 对，按 Slide|Speed / Rules / Steering / Anim 分组 (~1800 tok)
- `SprintBoostComponentSettings.h` — `USprintBoostComponentSettings`：冲刺时长/仅允许向前/最大前向夹角(0-180)/播速，4 对 (~700 tok)

## Source/Wildforge/Public/Core/

- `WildforgeGameMode.h` — AWildforgeGameMode (~200 tok)
- `WildforgePlayerController.h` — AWildforgePlayerController：ShowInventory()/HideInventory 切换 FInputModeGameAndUI ↔ GameOnly（光标显隐） (~500 tok)

## Source/Wildforge/Public/ItemSystem/Actors/

- `ItemMaster.h` — AItemMaster：物品 Actor 桩（暂空），被 FItemInformation::ItemClass 引用 (~200 tok)

## Source/Wildforge/Public/ItemSystem/Components/

- `ItemContainer.h` — 服务器权威物品容器组件：复制 Slots/SlotOccupied + 派生索引(FreeSlots/ItemIDToSlot/UsedCount)；增/删(AddItemStack/AddItemByID；RemoveItem 按槽位、RemoveAllItem 按 ItemID，均带 Quantity 默认 -1=全部)/整理/调整大小/查询 API；MoveOrMergeItem(From,To) 提供拖拽「放置」语义（同 ItemID 可堆叠且目标未满→合并、余数留源槽，否则交换）；OnContainerChanged 委托 + Server_* RPC（含 Server_MoveOrMerge） (~2900 tok)
- `PlayerInventory.h` — UPlayerInventory：UItemContainer 子类，构造时固定容器类型为 PlayerInventory (~300 tok)

## Source/Wildforge/Public/ItemSystem/Database/

- `ItemDatabaseSubsystem.h` — 物品定义数据库（GameInstanceSubsystem）：单一真相源，从项目设置取表 + int 键懒缓存 (~900 tok)
- `ItemSystemSettings.h` — UDeveloperSettings（Project Settings > Game > Item System）：TSoftObjectPtr<UDataTable> ItemTable，配置驱动物品表 (~500 tok)

## Source/Wildforge/Public/ItemSystem/Enums/

- `ContainerType.h` — EContainerType 枚举（PlayerStorage/PlayerInventory/…） (~200 tok)
- `ItemArmor.h` — EItemArmor 枚举 (~150 tok)
- `ItemRarity.h` — EItemRarity 枚举（Common…） (~150 tok)
- `ItemType.h` — EItemType 枚举（Other/…） (~200 tok)

## Source/Wildforge/Public/ItemSystem/Structs/

- `ItemInfo.h` — FItemInformation（FTableRowBase，需 include Engine/DataTable.h——别靠 SharedPCH，见 bug-014）：ItemID/ItemName/ItemDesc/ItemQuality（兼作堆叠数量）/ItemDamage/IsStackable/MaxStackSize/ItemCurHP/ItemMaxHP/ItemIcon/ItemType/ItemRarity/ItemArmor/ItemClass/UseAmmo/Ammo/AmmoMax (~900 tok)

## Source/Wildforge/Public/ItemSystem/UI/

- `InventorySlotDragDropOperation.h` — 拖拽操作（UDragDropOperation）：SourceSlotIndex/SourceContainerType/ItemInfo/SourceContainer + TWeakObjectPtr<UUserWidget> SourceWidget（弱引用避免源控件被网格刷新销毁后悬空）与 RestoreSourceWidget() 兜底复位，供 NativeOnDrop 判定来源并恢复显示 (~700 tok)
- `InventorySlotWidget.h` — 单格 Widget。**对外只留「来源」与控件自身状态**：InitializeSlot(容器,索引)/UnbindFromContainer/RefreshFromContainer + GetOwningContainer/GetSlotIndex，SetSelected/SetSlotStyle，以及可被蓝图覆写的 SetHighlight（BlueprintNativeEvent：C++ 默认染色 + HighlightColor）；**SetItemData(只收结构体)/ClearSlot/SetQuantity/SetItemHP/SetTopText/SetBottomText/SetItemIcon/SetItemStyle 都是 private 渲染细节**（数量唯一来源是 Item.ItemQuality，没有 CurrentQuantity）；HandleContainerChanged 处理容器广播；BindWidget TopText/BottomText/QuantityText/ItemHP/SlotStyle/ItemStyle/OverlayRoot/SizeBoxRoot；拖拽回调（NativeDestruct 退订 + 兜底复位不透明度）；状态 CurrentItem/bHasItemData + RefreshFromState()/IsSameAsCurrentData()；染色基准 NormalSlotColor/bNormalSlotColorCached（懒抓设计器值，未高亮时还原它，见 bug-015）；蓝图钩子 OnItemDataSet(结构体) (~2400 tok)
- `InventoryUserWidget.h` — 背包主界面：FInventoryThemeConfig 主题结构体；InitializeInventory/ShowInventoryPage/ShowCraftPage；BindWidget 按钮/Tab/WidgetSwitcher/ItemContainerGrid/动画；主题美化 ApplyInitialThemeStyles (~2200 tok)
- `ItemContainerGrid.h` — 背包网格 Widget（UniformGridPanel+ScrollBox）：`InitializeGrid(容器, InSlotsPerRow = 0)`——**<=0 表示沿用控件上配置的 SlotsPerRow**（WBP 设计器参数，调用方不再硬编码列数）；RefreshGrid 为全量重建入口；HandleContainerChanged 只处理结构（容量变化才增删槽位/重排）；Bind/UnbindToContainer；EnsureSlotCount（返回数量是否变化）/LayoutSlots；槽位内容由 UInventorySlotWidget 自己拉 (~720 tok)

## Source/Wildforge/Public/UI/

- `MainUserWidget.h` — UMainUserWidget：持有 UInventoryUserWidget 并切换其可见性 (~400 tok)

## Source/Wildforge/Public/Utils/

- `ComponentAuthorityGuard.h` — **WF_COMPONENT_AUTHORITY_GUARD(ReturnValue)**：UActorComponent 子类通用的开发期权威门禁宏（非权威端 WFLOG_ERROR + `return ReturnValue`，void 用 `void()`），供 SprintBoost/Blink 等新组件统一使用；内含「组件里不能写 HasAuthority（C3861）」的说明。（曾存在的 `AuthorityGuard.h` / `WF_AUTHORITY_GUARD` 已被用户删除——与本文档功能重复、仓库内无任何 TU 引用，不要恢复） (~400 tok)
- `WildforgeAuthority.h` — 权威端判断辅助（inline）：IsAuthoritativeForActorComponent(UActorComponent*) = GetOwner()->HasAuthority()；供组件子类使用（HasAuthority 是 AActor 的方法，组件上没有，直接写会 C3861） (~350 tok)
- `WildforgeLog.h` — 日志工具（UBlueprintFunctionLibrary）：Info/Warning/Error/WFLOG_* 宏；注册接口 WildforgeLogging::RegisterFileOutput；文件输出走异步设备 (FAsyncWriter) (~1000 tok)
