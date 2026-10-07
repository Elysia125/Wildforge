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

- `BaseCharacter.cpp` — ABaseCharacter 实现：构造函数只做两件事（bReplicates=true + 创建 UCharacterAttributes），BeginPlay/Tick/SetupPlayerInputComponent 均为空壳并各留一条生命周期日志。**加速与闪现已整体搬迁到 USprintBoostComponent / UBlinkComponent**，本文件不再有任何速度/位移逻辑 (~250 tok)

## Source/Wildforge/Private/Character/AnimNotify/

- `AnimNotifyAttackHit.cpp` — 命中帧通知实现：判 `MeshComp->GetOwner()->HasAuthority()` 才调 PerformDamageTrace（表现层特效各端本地调 PlayAttackEffects） (~250 tok)
- `AnimNotifyCombo.cpp` — 连击窗口通知实现：只发 `Server_NotifyComboWindow()`（不再直接写 bCanCombo——那是玩法状态，客户端写了无效） (~200 tok)

## Source/Wildforge/Private/Character/Components/

- `AttackComponent.cpp` — 攻击组件实现：顶部 WF_ATTACK_AUTHORITY_GUARD 宏（非权威端 WFLOG_ERROR + 安全返回，与 UItemContainer 同款）；Attack_Implementation 走重入守卫/冷却/连击窗口判定后 Multicast_PlayAttackMontage 并用局部变量推进段位；PlayAttackMontageInternal 是「无门禁内核」（选段用局部副本、不写玩法状态，客户端只播动画），PlayAttackMontage_Implementation 才带门禁；Multicast_PlayAttackMontage 服务器调权威入口、客户端调内核，之后每端各广播一次 OnAttackStarted；OnAttackMontageEnded 只认复制的 ActiveAttackMontage（不用会提前推进的下标查表）、清重入标志 → ResetAttackState → 广播 OnAttackFinished；OpenComboWindow 校验 bIsAttacking 并挂 AttackTimeoutHandle 兜底防动画异常卡死；PerformDamageTrace 用 SweepMultiByChannel + TSet 去重 + UGameplayStatics::ApplyDamage（引擎伤害管线解耦，组件只依赖 AActor）；Server_Attack/Server_NotifyComboWindow 两个 RPC (~3600 tok)
- `BlinkComponent.cpp` — 闪现组件实现：构造函数 SetIsReplicatedByDefault(true)、bCanEverTick=false；复制 LastBlinkTime/BlinkCount（COND_OwnerOnly，**不是** SimulatedOnly——那条件不发自主代理）；BlinkForward_Implementation 门禁 → 距离收敛（min(MaxDistance, MaxBlinkDistance)）→ 视线水平方向（失败退回角色前向）→ 胶囊体 SweepSingleByChannel 探整段（非可站立面直接取消、可站立面把落点贴回坡面）→ SetActorLocation(bSweep=true, TeleportPhysics) 并按法线退 0.1 + RoundToDouble 取整 → 向下探落点是否有可站立面（没有则 SetActorLocation 退回起点 + Multicast 失败表现 + return false）→ 按 bKeepVelocity 处理速度；Server_Blink/Server_BlinkForward 里才判 bCanBlink 与 IsBlinkReady 并更新 LastBlinkTime/BlinkCount；Multicast_PlayBlinkMontage/Effects 各端各播一次，Effects 里再广播 OnBlinkPerformed（地图落点 + 是否成功） (~2400 tok)
- `LandRollComponent.cpp` — 翻滚组件实现：构造函数 SetIsReplicatedByDefault(true)；BeginPlay 统一订阅 AnimInstance 的 OnMontageEnded + OnMontageBlendingOut（拿不到 AnimInstance 时在首次播放里补订，bMontageCallbacksBound 防重复绑定；EndPlay 退订 + 清兜底定时器）；LandRoll 顶部 WF_COMPONENT_AUTHORITY_GUARD，依次判总开关/World/重入/冷却（用 World->GetTimeSeconds 差分，不用 GetTimerElapsed）→ 写 bIsRolling/LastLandRollTime/LandRollCount → Multicast_PlayLandRollMontage（**Started 不在这里广播**，由 Multicast 分支在表现真的播起来后广播）→ 播起来了才 OpenRollTimeout（蒙太奇时长/播速 + 1s），没播起来则返回 false 并说明；PlayLandRollMontageInternal 校验蒙太奇为空/Mesh/AnimInstance、用 Montage_Play 返回值判成败、可选 Montage_JumpToSection、记 ActiveLandRollMontage；**Multicast_PlayLandRollMontage_Implementation 是每个端都会跑的分支**（客户端本地调用也只本地执行，见 Actor.cpp:5500-5519）：播成功 → `bRollPresentationActive = true` + 广播 OnLandRollStarted；播失败 → 一个事件都不广播，权威端就地 FinishLandRoll 收尾（否则 bIsRolling 永久 true 卡死冷却/重入）；OnLandRollMontageEnded **只认 ActiveLandRollMontage**（否则别人的蒙太奇结束会把翻滚状态清掉）；OnLandRollMontageBlendingOut 在 bFinishOnBlendOut 时提前解锁（判据是本端 bRollPresentationActive，**不是** COND_OwnerOnly 的 bIsRolling）；FinishLandRoll 清定时器/可选 Montage_Stop/复位本端表现标志（bIsRolling 只在权威端写）/仅在确实广播过 Started 时广播 OnLandRollFinished；ForceFinishLandRoll 给受击打断用；Multicast_PlayLandRollEffects **不加权威门禁**（客户端收到 Multicast 时必然非权威，加门禁等于客户端永远没特效）；另有 ResetLandRollCooldown + PlayLandRollEffects 蓝图钩子 (~4200 tok)
- `SprintBoostComponent.cpp` — 加速组件实现：构造函数 SetIsReplicatedByDefault(true) + Tick 默认关（只有 bDriveOwnerLocally 才逐帧）；复制 bBoostActive/BoostAlpha/LastTargetSpeed（COND_OwnerOnly）；BeginPlay **刻意不抓基准速度**（客户端此时 MaxWalkSpeed 还没复制到位，抓了会污染 ResetMaxSpeed 的目标）；**EvaluateSprintDirection()** 是方向判定的唯一出口（读 GetCurrentAcceleration() 的方向——**不能读 last input vector，服务器上恒为零**，见 bug-026；与 GetActorForwardVector 求 Acos 夹角；无输入/非 MOVE_Walking/无移动组件/功能关闭四种情况放行）；StartSpeedBoost_Implementation 门禁 → bCanSprint（关掉时顺带复位正在跑的加速）→ HoldThreshold/TargetMaxSpeed（必须 > 基准）校验 → **时长/间隔只校验有限性**（负数 = 用组件默认值，是合法输入，见 bug-028）→ 重入拒绝（不重开，避免速度一顿一顿）→ ResetMaxSpeed + CaptureBaseMaxSpeed → 记起始/目标/时长、Generation++、LastUpdateTime；BoostInterval>0 用循环定时器、=0 用 SetTimerForNextTick 链并在 TickSprintBoost 末尾按 Generation 续挂；**TickSprintBoost 先跑方向门控再累计 ElapsedTime**（非向前 → ResetMaxSpeed 并 return；每次回调都判，因为到顶后 bBoostActive 仍为 true）；到顶 ApplyMaxWalkSpeed(Target,1) 后只清定时器保留 bBoostActive；ElapsedTime 用 World->GetTimeSeconds() 差分；**ResetMaxSpeed 幂等安全复位**（`!bWasActive \|\| !bBaseSpeedValid` 时只清定时器/状态、**绝不写 MaxWalkSpeed**，见 bug-027）；**ApplyMaxWalkSpeed 有兜底闸门**（速度 <= 0 或非有限值 → WFLOG_ERROR 拒写，防止把角色钉死）；BeginPlay 打印移动组件真实 MaxWalkSpeed + 基准确认状态（**不能**打印未捕获时恒为 0 的 BaseMaxWalkSpeed，那会误导排查，见 bug-027）；BroadcastBoostStarted/Stopped 成对保护；TickLocalBoostVisualOnly 是客户端本地驱动分支（只广播不碰移动组件，**也跑同一套方向门控**保证本地预测与服务器一致）；GetSprintBoostDebugString 带方向段（向前/非向前 + 夹角 + 允许角 + 是否有输入）与基准可信标志；Multicast_PlaySprintMontage（SprintMontage 为空是合法状态，记 INFO 后跳过；不订阅 OnMontageEnded）/Multicast_PlaySprintEffects；Server_StartSpeedBoost/Server_StopSpeedBoost 两个 RPC（_Validate 与权威函数范围必须一致：负数一律放行交给默认值回退） (~3600 tok)
- `CharacterAttributes.cpp` — 属性组件实现：DOREPLIFETIME + OnRep 广播委托，SetHealth/SetMaxHealth 带 Clamp (~900 tok)

## Source/Wildforge/Private/Character/Player/

- `PlayerCharacter.cpp` — APlayerCharacter 实现：构造函数创建 UPlayerInventory(30) + UAttackComponent + ULandRollComponent + USprintBoostComponent + UBlinkComponent；顶部定义 MovementLockAttack/MovementLockLandRoll 两个 FName 来源常量；BeginPlay 先 RemoveDynamic 再 AddDynamic 订阅攻击/翻滚四个通知（组件缺失各记 WARNING），并按「加速/闪现组件缺失 = 能力整个消失」记 WARNING；EndPlay 全部退订；攻击与翻滚共用移动门控 **ApplyMovementLock(FName, bDisableMovement)/ReleaseMovementLock(FName)**：`TSet<FName> MovementLockHolders` 按来源记账（重复 Add 幂等 → 连击切段重复广播 OnAttackStarted 不会泄漏）+ `TSet<FName> MovementDisablingHolders` 硬锁子集（只有它清空才恢复移动模式，「翻滚中攻击」的交错才不会让软锁被硬锁语义污染）；**攻击 = 硬锁**（StopMovementImmediately + DisableMovement，会丢弃根运动，只给不需要位移的动作）、**翻滚 = 软锁**（只记账 + ResetSprintBoostOnMovementLock，完全不碰移动模式与速度，保住边跑边滚的惯性）；`bMovementDisabledByLock` 显式记录「移动是被这套锁禁掉的」+ `MovementModeBeforeLock` 记录原模式（不再靠 MovementMode == MOVE_None 反推）；查询 IsMovementLocked（有来源占用）/IsMovementDisabled（真的动不了）/GetMovementLockHoldersDebugString (~3200 tok)

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

- `BaseCharacter.h` — ABaseCharacter（ACharacter 子类）：只负责「所有角色都存在」的两件事——bReplicates=true + 持有 UCharacterAttributes；**加速与闪现已拆成独立组件挂到 APlayerCharacter**（本类不再有任何速度/位移 API，旧蓝图节点需重连 GetSprintBoostComponent/GetBlinkComponent） (~800 tok)

## Source/Wildforge/Public/Character/AnimNotify/

- `AnimNotifyAttackHit.h` — UAnimNotify_AttackHit：命中帧通知（实现在 .cpp 里判权威端后调 PerformDamageTrace，各端本地调 PlayAttackEffects） (~120 tok)
- `AnimNotifyCombo.h` — UAnimNotify_Combo：连击窗口通知（实现在 .cpp 里发 Server_NotifyComboWindow，不再直接写 bCanCombo） (~120 tok)

## Source/Wildforge/Public/Character/Components/

- `AttackComponent.h` — **服务器权威攻击组件**（本次改造）：可配置攻击参数（AttackCooldown/AttackDamage/AttackMontageList）+ 伤害检测参数（AttackRange 射程 / AttackTraceRadius 半径 / AttackTraceHeightOffset 胸口高度 / AttackTraceChannel 通道）；复制状态 bIsAttacking/bCanCombo/MontageSectionIndex/AttackMontageIndex/LastAttackTime/ActiveAttackMontage（全 COND_SimulatedOnly，服务器写、客户端只读）；通知 OnAttackStarted/OnAttackFinished（两段式生命周期，订阅者据此禁止/恢复移动）；权威函数 Attack/PerformDamageTrace/PlayAttackMontage/ResetAttackState（BlueprintNativeEvent+BlueprintAuthorityOnly，实现内还有 WF_ATTACK_AUTHORITY_GUARD 门禁）+ OpenComboWindow；客户端入口 Server_Attack/RequestAttack/Server_NotifyComboWindow；表现同步 Multicast_PlayAttackMontage/Multicast_PlayAttackEffects；私有 bAttackMontageInProgress（服务器重入守卫，不复制）与 AttackTimeoutHandle（动画异常兜底）；无门禁内核 PlayAttackMontageInternal(段位, 淡入时长)（客户端只播动画不写状态）；段位用局部变量避免客户端改玩法状态；连击=窗口内点击**立刻切播下一段**（Montage_PlayWithBlendIn 交叉淡入，不等当前段播完），bAttackInputConsumed 防一次按键连完整套、bAdvancingCombo 把自己切段引发的 OnMontageEnded(bInterrupted) 与「链结束」区分开 (~4600 tok)
- `BlinkComponent.h` — **服务器权威闪现组件**（新增，从 ABaseCharacter 拆出）：配置 bCanBlink/MaxBlinkDistance/BlinkCooldown/bKeepVelocityAfterBlink/BlinkTraceChannel + 可选表现 BlinkMontage/BlinkMontagePlayRate；复制 LastBlinkTime/BlinkCount（COND_OwnerOnly）；权威函数 BlinkForward（BlueprintNativeEvent；**枚举参数必须写裸 ECollisionChannel、默认值必须与 `=` 同行**，否则 _Implementation 签名对不上，见 bug-023）/BlinkToConfiguredDistance/ResetBlinkCooldown；查询 IsBlinkReady/GetBlinkCooldownRemaining/GetBlinkDebugString；通知 OnBlinkPerformed（落点 + 是否成功）；表现 Multicast_PlayBlinkMontage/Effects + 蓝图钩子 PlayBlinkEffects；客户端入口 Server_Blink/Server_BlinkForward (~1800 tok)
- `CharacterAttributes.h` — UCharacterAttributes：复制 Health/MaxHealth（ReplicatedUsing OnRep 广播 FOnHealthChanged/FOnMaxHealthChanged），Setter 为 BlueprintAuthorityOnly，且 SetHealth/SetMaxHealth 内联加非权威端门禁（IsAuthoritativeForActorComponent + WFLOG_ERROR） (~900 tok)
- `LandRollComponent.h` — **服务器权威翻滚组件**：配置 LandRollMontage/MontagePlayRate/LandRollSectionName/bFinishOnBlendOut/bCanLandRoll/bStopMovementOnStart + LandRollCooldown；复制 bIsRolling/LastLandRollTime/LandRollCount/LandRollCooldown/ActiveLandRollMontage（**COND_OwnerOnly = 只有拥有者客户端收得到，别人收不到**，想让所有人都读到必须去掉 condition）；**新增不复制的 `bRollPresentationActive`**（各端自维护，生命周期广播的唯一判据，见 Multicast_PlayLandRollMontage_Implementation）；权威函数 LandRoll（BlueprintNativeEvent+门禁，表现没起来时返回 false 且状态已回滚）/ResetLandRollCooldown/ForceFinishLandRoll；通知 OnLandRollStarted/OnLandRollFinished（**每端各自本地广播**，Started 由 Multicast 分支在表现播起来后发）；表现 Multicast_PlayLandRollMontage(段名)/Multicast_PlayLandRollEffects + 蓝图钩子 PlayLandRollEffects；受保护回调 OnLandRollMontageEnded/OnLandRollMontageBlendingOut + HandleLandRollTimeout 兜底 + PlayLandRollMontageInternal 无门禁内核 + FinishLandRoll；查询 GetLandRollDebugString/IsLandRollReady/GetLandRollCooldownRemaining/**IsRollPresentationActive（所有端都有效，动画蓝图/UI 该用它而不是 bIsRolling）**；客户端入口 Server_LandRoll (~2600 tok)
- `SprintBoostComponent.h` — **服务器权威加速组件**（新增，从 ABaseCharacter 拆出）：配置 bCanSprint/DefaultBoostDuration/DefaultBoostInterval/MinTargetSpeed + **方向门控** bSprintOnlyForward（默认 true）/MaxForwardSprintAngle（默认 60°）/SprintDirectionInputThreshold + 可选表现 SprintMontage/SprintMontagePlayRate + 受保护 bDriveOwnerLocally（本地驱动开关，**必须在 protected**：UHT 不允许私有成员带 BlueprintReadOnly，见 bug-023）；复制 bBoostActive/BoostAlpha/LastTargetSpeed（COND_OwnerOnly，**不要** SimulatedOnly——不发自主代理）；权威函数 StartSpeedBoost（BlueprintNativeEvent；BoostDuration/BoostInterval <0 = 用组件默认值）/ResetMaxSpeed/SetBaseMaxSpeed/StopSpeedBoost；查询 IsSpeedBoostActive/GetSpeedBoostAlpha/GetBaseMaxWalkSpeed(Crouched)/GetSprintBoostDebugString + 方向查询 GetSprintInputForwardAngle/IsSprintInputDirectionForward；通知 OnSprintBoostUpdated(Alpha)/OnSprintBoostStarted/OnSprintBoostStopped；表现 Multicast_PlaySprintMontage(FName)/Multicast_PlaySprintEffects + PlaySprintEffects；私有 FSprintBoostState（起始/目标/基准速度对 **+ bBaseSpeedValid**「基准值是否已捕获」——`ResetMaxSpeed` 靠它决定能不能写回，见 bug-027/CrouchSpeedRatio/ElapsedTime/BoostDuration/LastUpdateTime/FTimerHandle/Generation/bBoostActive/bIntervalTimer/bStartBroadcastPending）+ **FSprintDirectionInfo + EvaluateSprintDirection()**（夹角/是否有输入/是否算向前/原因，单一判定出口）+ TickSprintBoost 每帧续挂 + BroadcastBoostStarted/Stopped 成对保护；客户端入口 Server_StartSpeedBoost/Server_StopSpeedBoost (~2900 tok)
- `structs/AttackMontageData.h` — FAttackMontageData：Montage + SectionNames（TArray<FName>，默认 {NAME_None}），攻击蒙太奇分段数据 (~150 tok)

## Source/Wildforge/Public/Character/Player/

- `PlayerCharacter.h` — APlayerCharacter：持有 UPlayerInventory（30 格）+ UAttackComponent + ULandRollComponent + USprintBoostComponent + UBlinkComponent，五个 Get* 访问器（GetInventory/GetAttackComponent/GetLandRollComponent/GetSprintBoostComponent/GetBlinkComponent）；**这是旧蓝图节点重连加速/闪现 RPC 的目标**；移动门控按来源记账：`MovementLockHolders`（全部持有者）+ `MovementDisablingHolders`（**硬锁子集，恢复移动模式只看它**）+ `bMovementDisabledByLock`/`MovementModeBeforeLock`，公开 ApplyMovementLock(FName, bDisableMovement)/ReleaseMovementLock(FName) + IsMovementLocked/IsMovementDisabled/GetMovementLockHoldersDebugString；类注释里写明了「硬锁会丢弃根运动，所以翻滚必须用软锁」与「持有锁的组件必须与角色同生共死」两条前置条件 (~2400 tok)

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

- `ComponentAuthorityGuard.h` — **WF_COMPONENT_AUTHORITY_GUARD(ReturnValue)**：UActorComponent 子类通用的开发期权威门禁宏（非权威端 WFLOG_ERROR + `return ReturnValue`，void 用 `void()`），供 SprintBoost/Blink 等新组件统一使用；内含「组件里不能写 HasAuthority（C3861）」的说明。（曾存在的 `AuthorityGuard.h` / `WF_AUTHORITY_GUARD` 已被用户删除——与本文档功能重复、仓库内无任何 TU 引用，不要恢复） (~400 tok)
- `WildforgeAuthority.h` — 权威端判断辅助（inline）：IsAuthoritativeForActorComponent(UActorComponent*) = GetOwner()->HasAuthority()；供组件子类使用（HasAuthority 是 AActor 的方法，组件上没有，直接写会 C3861） (~350 tok)
- `WildforgeLog.h` — 日志工具（UBlueprintFunctionLibrary）：Info/Warning/Error/WFLOG_* 宏；注册接口 WildforgeLogging::RegisterFileOutput；文件输出走异步设备 (FAsyncWriter) (~1000 tok)
