# anatomy.md

> Auto-maintained by OpenWolf. Last scanned: 2026-10-07T05:33:50.958Z
> Files: 65 tracked | Anatomy hits: 0 | Misses: 0

## ./

- `.clang-format` — 基于 Unreal Engine 风格的配置 (~116 tok)
- `.clangd` — UBT 的 -Mode=GenerateClangDatabase 以 clang-cl（CL 驱动）生成 compile_commands.json： (~101 tok)
- `.gitignore` — Git ignore rules（排除体积过大的第三方资源包 `Content/ProceduralNaturePack/`，本机专属文件 CLAUDE.LOCAL.md 不入库） (~84 tok)
- `.ignore` (~30 tok)
- `CLAUDE.LOCAL.md` — 本机专属：引擎/项目/LLVM 路径与编译验证命令（不入 git） (~900 tok)
- `CLAUDE.md` — 项目级指令与强制约束（跨成员通用，入库）：项目概览 / 构建命令 / 架构 / 约定与坑 / 上下文文件分工（buglog↔cerebrum 写法约定）/ 强制约束 (~3800 tok)
- `README.md` — Project documentation (~11 tok)
- `Wildforge.code-workspace` (~4460 tok)
- `Wildforge.uproject` (~124 tok)

## C:/Users/xf317/AppData/Local/Temp/

- `wf_clangd_info.ps1` (~82 tok)
- `wf_fix_clangd.js` — Declares fs (~96 tok)
- `wf_wolf_record.js` — fs: eolOf (~992 tok)

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

- `BaseCharacter.cpp` — ABaseCharacter 实现：构造函数创建 UCharacterAttributes 组件；BeginPlay 抓基准 MaxWalkSpeed/MaxWalkSpeedCrouched 并算出 CrouchSpeedRatio；StartSpeedBoost 按 BoostInterval 分两种刷新（>0 用循环定时器，=0 用 SetTimerForNextTick 每帧链，回调自己续挂——SetTimer 传 0 在引擎里是「清掉定时器」）并在 ElapsedTime/BoostDuration 上线性插值移动组件速度（到顶停表保持，bBoostActive 仍是加速中）；ApplyMaxWalkSpeed 让蹲伏速度按基准比例跟随；TickSprintBoost 用 World->GetTimeSeconds() 差分累计并靠 Generation 防重入续挂；ResetMaxSpeed 精确还原两个基准值；BlinkForward 用视线水平方向 + 胶囊体 Sweep 做 Teleport 闪现并校验落点有可站立面；Server_StartSpeedBoost/Server_StopSpeedBoost/Server_BlinkForward/Server_Blink 四个 RPC 转调权威函数 (~4700 tok)

## Source/Wildforge/Private/Character/AnimNotify/

- `AnimNotifyAttackHit.cpp` — 命中帧通知实现：判 `MeshComp->GetOwner()->HasAuthority()` 才调 PerformDamageTrace（表现层特效各端本地调 PlayAttackEffects） (~250 tok)
- `AnimNotifyCombo.cpp` — 连击窗口通知实现：只发 `Server_NotifyComboWindow()`（不再直接写 bCanCombo——那是玩法状态，客户端写了无效） (~200 tok)

## Source/Wildforge/Private/Character/Components/

- `AttackComponent.cpp` — 攻击组件实现：顶部 WF_ATTACK_AUTHORITY_GUARD 宏（非权威端 WFLOG_ERROR + 安全返回，与 UItemContainer 同款）；Attack_Implementation 走重入守卫/冷却/连击窗口判定后 Multicast_PlayAttackMontage 并用局部变量推进段位；PlayAttackMontageInternal 是「无门禁内核」（选段用局部副本、不写玩法状态，客户端只播动画），PlayAttackMontage_Implementation 才带门禁；Multicast_PlayAttackMontage 服务器调权威入口、客户端调内核，之后每端各广播一次 OnAttackStarted；OnAttackMontageEnded 只认复制的 ActiveAttackMontage（不用会提前推进的下标查表）、清重入标志 → ResetAttackState → 广播 OnAttackFinished；OpenComboWindow 校验 bIsAttacking 并挂 AttackTimeoutHandle 兜底防动画异常卡死；PerformDamageTrace 用 SweepMultiByChannel + TSet 去重 + UGameplayStatics::ApplyDamage（引擎伤害管线解耦，组件只依赖 AActor）；Server_Attack/Server_NotifyComboWindow 两个 RPC (~3600 tok)
- `CharacterAttributes.cpp` — 属性组件实现：DOREPLIFETIME + OnRep 广播委托，SetHealth/SetMaxHealth 带 Clamp (~900 tok)

## Source/Wildforge/Private/Character/Player/

- `PlayerCharacter.cpp` — APlayerCharacter 实现：构造函数创建 UPlayerInventory 并 InitializeContainer(30) + UAttackComponent；BeginPlay 订阅 OnAttackStarted/OnAttackFinished；HandleAttackStarted 记 MovementModeBeforeAttack、先 StopMovementImmediately 再 DisableMovement（并在服务器侧顺带 ResetMaxSpeed 断掉加速）；HandleAttackFinished 仅在当前是 MOVE_None 时还原移动模式再清残留速度（回调不配对时不动移动模式） (~1400 tok)

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
- `InventoryUserWidget.cpp` — 主界面实现：NativeConstruct 取容器 + 初始化网格；Tab 点击切 WidgetSwitcher 页并更新高亮/反馈动画；整理按钮调 Server_OrganizeContainer（RPC，不可直接调 OrganizeContainer）；ApplyInitialThemeStyles 应用配色与阴影 (~2600 tok)
- `ItemContainerGrid.cpp` — 网格实现：NativeConstruct 从拥有者 Pawn 查找 UItemContainer 并 InitializeGrid；HandleContainerChanged 只做结构（容量变了才 EnsureSlotCount+LayoutSlots，不整表刷内容）；RefreshGrid 为全量入口（保证数量/布局 + 逐格 InitializeSlot 重绑并拉数据）；EnsureSlotCount 补建时当场 InitializeSlot、移除时先 UnbindFromContainer (~1600 tok)

## Source/Wildforge/Private/UI/

- `MainUserWidget.cpp` — 主界面 Widget 实现 (~300 tok)

## Source/Wildforge/Private/Utils/

- `WildforgeLog.cpp` — 实现：FWildforgeLogDevice（FOutputDevice）按等级分派到 3 个 FLevelFileWriter（各含 FAsyncWriter 后台线程 + 独立锁），UTF-8 无 BOM，跨日切目录；仅此处允许内部 UE_LOG(LogWildforge, …) (~1600 tok)

## Source/Wildforge/Public/Character/

- `BaseCharacter.h` — ABaseCharacter（ACharacter 子类）：bReplicates=true，持有 UCharacterAttributes 组件；私有 FSprintBoostState（起始/目标速度、基准速度对 + CrouchSpeedRatio、ElapsedTime/BoostDuration/LastUpdateTime、FTimerHandle、Generation 代次、bBoostActive/bIntervalTimer）；权威函数 StartSpeedBoost/ResetMaxSpeed/SetBaseMaxSpeed（可带蹲伏基准）/BlinkForward（注意 BlinkForward 的 FVector& 输出引用必须排在带默认值的参数之前）+ 查询 IsSpeedBoostActive/GetSpeedBoostAlpha/GetBaseMaxWalkSpeed + Server_* RPC ×4 + DefaultMaxBlinkDistance (~2700 tok)

## Source/Wildforge/Public/Character/AnimNotify/

- `AnimNotifyAttackHit.h` — UAnimNotify_AttackHit：命中帧通知（实现在 .cpp 里判权威端后调 PerformDamageTrace，各端本地调 PlayAttackEffects） (~120 tok)
- `AnimNotifyCombo.h` — UAnimNotify_Combo：连击窗口通知（实现在 .cpp 里发 Server_NotifyComboWindow，不再直接写 bCanCombo） (~120 tok)

## Source/Wildforge/Public/Character/Components/

- `AttackComponent.h` — **服务器权威攻击组件**（本次改造）：可配置攻击参数（AttackCooldown/AttackDamage/AttackMontageList）+ 伤害检测参数（AttackRange 射程 / AttackTraceRadius 半径 / AttackTraceHeightOffset 胸口高度 / AttackTraceChannel 通道）；复制状态 bIsAttacking/bCanCombo/MontageSectionIndex/AttackMontageIndex/LastAttackTime/ActiveAttackMontage（全 COND_SimulatedOnly，服务器写、客户端只读）；通知 OnAttackStarted/OnAttackFinished（两段式生命周期，订阅者据此禁止/恢复移动）；权威函数 Attack/PerformDamageTrace/PlayAttackMontage/ResetAttackState（BlueprintNativeEvent+BlueprintAuthorityOnly，实现内还有 WF_ATTACK_AUTHORITY_GUARD 门禁）+ OpenComboWindow；客户端入口 Server_Attack/RequestAttack/Server_NotifyComboWindow；表现同步 Multicast_PlayAttackMontage/Multicast_PlayAttackEffects；私有 bAttackMontageInProgress（服务器重入守卫，不复制）与 AttackTimeoutHandle（动画异常兜底）；无门禁内核 PlayAttackMontageInternal(段位, 淡入时长)（客户端只播动画不写状态）；段位用局部变量避免客户端改玩法状态；连击=窗口内点击**立刻切播下一段**（Montage_PlayWithBlendIn 交叉淡入，不等当前段播完），bAttackInputConsumed 防一次按键连完整套、bAdvancingCombo 把自己切段引发的 OnMontageEnded(bInterrupted) 与「链结束」区分开 (~4600 tok)
- `CharacterAttributes.h` — UCharacterAttributes：复制 Health/MaxHealth（ReplicatedUsing OnRep 广播 FOnHealthChanged/FOnMaxHealthChanged），Setter 为 BlueprintAuthorityOnly，且 SetHealth/SetMaxHealth 内联加非权威端门禁（IsAuthoritativeForActorComponent + WFLOG_ERROR） (~900 tok)
- `structs/AttackMontageData.h` — FAttackMontageData：Montage + SectionNames（TArray<FName>，默认 {NAME_None}），攻击蒙太奇分段数据 (~150 tok)

## Source/Wildforge/Public/Character/Player/

- `PlayerCharacter.h` — APlayerCharacter：持有 UPlayerInventory（30 格）+ UAttackComponent；GetInventory()/GetAttackComponent()；**订阅攻击组件 OnAttackStarted/OnAttackFinished 做移动门控**（HandleAttackStarted/HandleAttackFinished + MovementModeBeforeAttack 本地缓存） (~1100 tok)

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
- `ItemContainerGrid.h` — 背包网格 Widget（UniformGridPanel+ScrollBox）：InitializeGrid 绑定容器并订阅 OnContainerChanged（**只处理结构**：容量变化才增删槽位/重排），RefreshGrid 为全量重建入口；Bind/UnbindToContainer；EnsureSlotCount（返回数量是否变化）/LayoutSlots；槽位内容由 UInventorySlotWidget 自己拉 (~1200 tok)

## Source/Wildforge/Public/UI/

- `MainUserWidget.h` — UMainUserWidget：持有 UInventoryUserWidget 并切换其可见性 (~400 tok)

## Source/Wildforge/Public/Utils/

- `WildforgeAuthority.h` — 权威端判断辅助（inline）：IsAuthoritativeForActorComponent(UActorComponent*) = GetOwner()->HasAuthority()；供组件子类使用（HasAuthority 是 AActor 的方法，组件上没有，直接写会 C3861） (~350 tok)
- `WildforgeLog.h` — 日志工具（UBlueprintFunctionLibrary）：Info/Warning/Error/WFLOG_* 宏；注册接口 WildforgeLogging::RegisterFileOutput；文件输出走异步设备 (FAsyncWriter) (~1000 tok)
