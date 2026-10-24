#include "ItemSystem/Components/ContainerInteractionComponent.h"

#include "Blueprint/UserWidget.h"
#include "Core/WildforgePlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ItemSystem/Settings/ContainerInteractionSettings.h"
#include "Net/UnrealNetwork.h"
#include "UI/MainUserWidget.h"
#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"
#include "World/ChestActor.h"
#include "World/UI/ChestWindowWidget.h"

UContainerInteractionComponent::UContainerInteractionComponent() {
  PrimaryComponentTick.bCanEverTick = false;

  // 组件参与复制的前提：宿主 Actor 的 bReplicates 也为 true
  // （APlayerCharacter 继承的 ABaseCharacter 已保证）。漏了这行所有
  // DOREPLIFETIME_* 都会静默失效，见 bug-093。
  SetIsReplicatedByDefault(true);
}

void UContainerInteractionComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 只有拥有者客户端需要知道「我当前开着哪个容器」。
  DOREPLIFETIME_CONDITION(UContainerInteractionComponent, ActiveContainer,
                          COND_OwnerOnly);

  // 交互距离是角色生命周期内不变的静态调参：每个端都要读，但不能让客户端
  // 读本地 ini（玩家可改），所以由服务器读 ini 后随出生束下发。
  DOREPLIFETIME_CONDITION(UContainerInteractionComponent,
                          MaxInteractionDistance, COND_InitialOnly);
}

void UContainerInteractionComponent::BeginPlay() {
  Super::BeginPlay();

  // 只有权威端读 ini；客户端等方法内部会直接返回，只认复制下来的值。
  ApplyGameplaySettingsOverrides();
}

void UContainerInteractionComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason) {
  if (IsAuthoritativeForActorComponent(this)) {
    // 角色死亡 / 断线 / 换关卡：把当前宝箱从「打开者集合」里移除。
    // 最后一名打开者离开时服务器会播放关箱表现；否则箱子会永久停在打开状态。
    UItemContainer *CurrentContainer = GetActiveContainer();
    if (AChestActor *Chest =
            CurrentContainer
                ? Cast<AChestActor>(CurrentContainer->GetOwner())
                : nullptr) {
      WFLOG_INFO("[容器交互] %s 销毁，关闭当前宝箱 %s。",
                 *GetNameSafe(GetOwner()), *GetNameSafe(Chest));
      Chest->CloseChest(GetOwningPlayerController());
    }
    SetActiveContainer(nullptr);
  } else {
    // 客户端：先把本地状态清空，再让 OnRep 销毁窗口。直接 DestroyChestWindow
    // 会让窗口的 NativeDestruct 看到 ActiveContainer 仍是当前容器，从而在
    // 组件销毁阶段再发一次关闭 RPC。
    ActiveContainer = nullptr;
    OnRep_ActiveContainer();
  }

  Super::EndPlay(EndPlayReason);
}

void UContainerInteractionComponent::ApplyGameplaySettingsOverrides() {
  // 客户端合法地会跑到 BeginPlay，这里**静默返回**（不是误用），
  // 所以用 IsAuthoritativeForActorComponent 而不是会记 ERROR 的权威门禁宏。
  if (!IsAuthoritativeForActorComponent(this)) {
    return;
  }

  const UContainerInteractionSettings *Settings =
      UContainerInteractionSettings::Get();
  if (!Settings) {
    WFLOG_ERROR("[配置] %s 取不到 UContainerInteractionSettings（CDO 为空），"
                "容器交互距离保留类默认值 %.1f。",
                *GetNameSafe(GetOwner()), MaxInteractionDistance);
    return;
  }

  const float ClassDefault = MaxInteractionDistance;
  if (Settings->bOverride_MaxInteractionDistance) {
    // 与配置头文件的 ClampMin 保持一致：ini 是手写文本，绕过了面板的 Clamp。
    const float IniValue = Settings->MaxInteractionDistance;
    MaxInteractionDistance = FMath::Max(0.f, IniValue);
    WFLOG_INFO("[配置] %s 容器交互距离被 ini 覆盖：类默认值 %.1f -> 生效 %.1f"
               "（ini=%.1f）。",
               *GetNameSafe(GetOwner()), ClassDefault, MaxInteractionDistance,
               IniValue);
  } else {
    WFLOG_INFO("[配置] %s 容器交互距离没有 ini 覆盖，用类默认值 %.1f。",
               *GetNameSafe(GetOwner()), ClassDefault);
  }
}

APlayerController *
UContainerInteractionComponent::GetOwningPlayerController() const {
  APawn *OwningPawn = Cast<APawn>(GetOwner());
  if (!OwningPawn) {
    return nullptr;
  }
  return Cast<APlayerController>(OwningPawn->GetController());
}

bool UContainerInteractionComponent::HasInteractionRange(
    UItemContainer *Container, APlayerController *Player) const {
  if (!Container || !Player) {
    return false;
  }

  AActor *ContainerActor = Container->GetOwner();
  if (!ContainerActor) {
    WFLOG_WARNING("[容器交互] 容器 %s 没有宿主 Actor，无法判断交互距离。",
                  *GetNameSafe(Container));
    return false;
  }

  // 玩家自己的背包 / 快捷栏就挂在角色上：不受距离限制（否则转身都开不了背包）。
  if (ContainerActor == GetOwner()) {
    return true;
  }

  const APawn *PlayerPawn = Player->GetPawn();
  if (!PlayerPawn) {
    return false;
  }

  const float Distance = FVector::Dist(PlayerPawn->GetActorLocation(),
                                       ContainerActor->GetActorLocation());
  const float MaxDistance = FMath::Max(0.f, MaxInteractionDistance);
  if (Distance > MaxDistance) {
    WFLOG_WARNING("[容器交互] %s 距离容器 %s 太远（%.1f > %.1f cm），拒绝。",
                  *GetNameSafe(GetOwner()), *GetNameSafe(ContainerActor),
                  Distance, MaxDistance);
    return false;
  }
  return true;
}

bool UContainerInteractionComponent::CanInteractWith(
    UItemContainer *Container) const {
  if (!Container) {
    return false;
  }

  APlayerController *PC = GetOwningPlayerController();
  if (!PC) {
    // 客户端 Pawn 尚未被占有 / 服务器端角色尚未被 Controller 控制时会出现。
    WFLOG_WARNING("[容器交互] %s 还没有 PlayerController，无法校验容器权限。",
                  *GetNameSafe(GetOwner()));
    return false;
  }

  if (!Container->HasAccess(PC)) {
    WFLOG_WARNING("[容器交互] 玩家 %s 无权访问容器 %s（权限模式=%d）。",
                  *GetNameSafe(PC), *GetNameSafe(Container),
                  static_cast<int32>(Container->GetAccessMode()));
    return false;
  }

  return HasInteractionRange(Container, PC);
}

void UContainerInteractionComponent::SetActiveContainer(
    UItemContainer *Container) {
  if (ActiveContainer.Get() == Container) {
    return;
  }

  ActiveContainer = Container;

  // 服务器不会收到自己的 RepNotify：手动调一次，让监听服务器的本地 UI
  // 也能收到 OnActiveContainerChanged。拥有者客户端会由引擎在复制后调用。
  OnRep_ActiveContainer();
}

void UContainerInteractionComponent::OnRep_ActiveContainer() {
  // 通过 getter 读：容器在打开期间被销毁时按「无」处理，避免把
  // pending-kill 指针广播给 UI。
  UItemContainer *Container = GetActiveContainer();
  WFLOG_INFO("[容器交互] %s 当前打开的容器：%s", *GetNameSafe(GetOwner()),
             Container ? *GetNameSafe(Container) : TEXT("无"));

  // 宝箱容器 -> 创建 / 销毁对应窗口；非宝箱容器不创建窗口。
  UpdateChestWindow(Container);

  OnActiveContainerChanged.Broadcast(Container);
}

void UContainerInteractionComponent::UpdateChestWindow(
    UItemContainer *Container) {
  APlayerController *PC = GetOwningPlayerController();
  if (!PC || !PC->IsLocalController()) {
    // 专用服务器与模拟代理没有本地 UMG；权威端只负责复制状态。
    DestroyChestWindow();
    return;
  }

  AChestActor *Chest =
      Container ? Cast<AChestActor>(Container->GetOwner()) : nullptr;
  if (!Chest) {
    DestroyChestWindow();
    return;
  }

  // 已经是同一个箱子 / 同一个容器的窗口：不重建（避免窗口闪烁与动画重播）。
  if (ChestWindow && ChestWindow->GetChestActor() == Chest &&
      ChestWindow->GetContainer() == Container) {
    return;
  }

  DestroyChestWindow();

  const TSubclassOf<UChestWindowWidget> WidgetClass =
      Chest->GetChestWindowClass();
  if (!WidgetClass) {
    WFLOG_ERROR("[宝箱UI] 宝箱 %s 没有配置 ChestWindowClass，无法创建窗口"
                "（服务器入口应已拒绝；检查类默认值是否在客户端可用）。",
                *GetNameSafe(Chest));
    return;
  }

  UChestWindowWidget *NewWindow =
      CreateWidget<UChestWindowWidget>(PC, WidgetClass);
  if (!NewWindow) {
    WFLOG_ERROR("[宝箱UI] 创建宝箱窗口失败（类=%s，玩家=%s）。",
                *GetNameSafe(WidgetClass.Get()), *GetNameSafe(PC));
    return;
  }

  ChestWindow = NewWindow;

  // 先注入数据再挂到视口：WBP 的 OnChestWindowInitialized 能立刻读到容器；
  // 网格控件在 CreateWidget 时已经完成绑定。
  NewWindow->InitializeChestWindow(Chest, Container);
  NewWindow->AddToViewport(10);

  // AddToViewport 默认把控件铺满全屏；按 WBP Class Defaults 里的 WindowSize
  // 改成固定尺寸并按视口中心摆放。
  NewWindow->ApplyWindowLayout();

  // 开箱期间只接收 UI 输入：移动 / 攻击 / 交互等游戏输入全部暂停，
  // 避免玩家一边开箱一边走位。
  FInputModeUIOnly InputMode;
  if (NewWindow->IsFocusable()) {
    // 只有 WBP 在 Class Defaults 里开了 Is Focusable 才设置焦点：
    // UUserWidget 默认不接收键盘焦点，强行设置会让引擎记一条
    // LogPlayerController Error。鼠标点击不依赖焦点。
    InputMode.SetWidgetToFocus(NewWindow->TakeWidget());
  } else {
    WFLOG_INFO("[宝箱UI] %s 未开启 Is Focusable：窗口只响应鼠标；"
               "需要键盘操作时请在 WBP Class Defaults 里打开。",
               *NewWindow->GetClass()->GetName());
  }
  InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
  PC->SetInputMode(InputMode);
  PC->SetShowMouseCursor(true);
  bAppliedChestInputMode = true;

  WFLOG_INFO("[宝箱UI] 玩家 %s 创建宝箱窗口 %s（宝箱=%s，容器=%s，等级=%d）。",
             *GetNameSafe(PC), *GetNameSafe(NewWindow), *GetNameSafe(Chest),
             *GetNameSafe(Container),
             static_cast<int32>(Chest->GetChestLevel()));
}

void UContainerInteractionComponent::DestroyChestWindow() {
  if (ChestWindow) {
    WFLOG_INFO("[宝箱UI] %s 销毁宝箱窗口 %s。", *GetNameSafe(GetOwner()),
               *GetNameSafe(ChestWindow.Get()));
    // RemoveFromParent 会触发 NativeDestruct；此时 ActiveContainer 已经为空或
    // 已指向新容器，窗口里的 RequestCloseChest 不会重复发关闭 RPC。
    ChestWindow->RemoveFromParent();
    ChestWindow = nullptr;
  }

  if (!bAppliedChestInputMode) {
    return;
  }
  bAppliedChestInputMode = false;

  APlayerController *PC = GetOwningPlayerController();
  if (!PC || !PC->IsLocalController()) {
    return;
  }

  // 宝箱窗口用的是 UIOnly：背包还开着时必须恢复背包自己的 GameAndUI，
  // 否则玩家会卡在「能点 UI、但不能移动」的状态（两个窗口可以同时存在）。
  if (IsInventoryOpenOnOwningPlayer()) {
    FInputModeGameAndUI InventoryInputMode;
    InventoryInputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InventoryInputMode.SetHideCursorDuringCapture(false);
    PC->SetInputMode(InventoryInputMode);
    PC->SetShowMouseCursor(true);
    WFLOG_INFO("[宝箱UI] 关闭宝箱窗口时背包仍打开，恢复 GameAndUI 输入模式。");
    return;
  }

  PC->SetInputMode(FInputModeGameOnly());
  PC->SetShowMouseCursor(false);
  WFLOG_INFO("[宝箱UI] %s 恢复纯游戏输入模式。", *GetNameSafe(GetOwner()));
}

bool UContainerInteractionComponent::IsInventoryOpenOnOwningPlayer() const {
  const APlayerController *PC = GetOwningPlayerController();
  const AWildforgePlayerController *WildforgePC =
      Cast<AWildforgePlayerController>(PC);
  return WildforgePC && WildforgePC->MainUserWidget &&
         WildforgePC->MainUserWidget->bIsInventoryOpen;
}

// ===== 客户端 -> 服务器：容器交互请求 =====

bool UContainerInteractionComponent::Server_RequestOpenContainer_Validate(
    UItemContainer *Container) {
  return Container != nullptr;
}

void UContainerInteractionComponent::Server_RequestOpenContainer_Implementation(
    UItemContainer *Container) {
  APlayerController *PC = GetOwningPlayerController();
  if (!PC) {
    WFLOG_WARNING("[容器交互] 打开容器请求被拒：%s 没有 PlayerController。",
                  *GetNameSafe(GetOwner()));
    return;
  }
  if (PC->GetPawn() != GetOwner()) {
    WFLOG_WARNING(
        "[容器交互] 打开容器请求被拒：%s 不是该 Controller 当前控制的 "
        "Pawn。",
        *GetNameSafe(GetOwner()));
    return;
  }
  if (!Container) {
    WFLOG_WARNING("[容器交互] 打开容器请求被拒：容器为空。");
    return;
  }
  if (!CanInteractWith(Container)) {
    WFLOG_WARNING("[容器交互] 玩家 %s 打开容器 %s 被拒绝（权限或距离不满足）。",
                  *GetNameSafe(PC), *GetNameSafe(Container));
    return;
  }

  // 切换容器时先别改 ActiveContainer：新的宝箱可能因为权限 / 窗口类校验失败，
  // 那时应该保留原来的活动容器，而不是把玩家的窗口无条件关掉。
  UItemContainer *PreviousContainer = GetActiveContainer();

  // 宝箱：先让宝箱做业务校验（权限 / 窗口类 / 容器状态）并触发开箱表现。
  // 窗口显示不在这里直接做——下面写入 ActiveContainer 后，拥有者客户端的
  // OnRep_ActiveContainer 会按 ChestWindowClass 创建窗口。
  // 注意调用的是 OpenChestOnServer：OpenChest 是给蓝图用的统一入口，
  // 它会再转回本函数，直接调会递归。
  if (AChestActor *Chest = Cast<AChestActor>(Container->GetOwner())) {
    if (!Chest->OpenChestOnServer(PC)) {
      WFLOG_WARNING("[容器交互] 玩家 %s 打开宝箱 %s 失败"
                    "（权限 / 窗口类 / 容器状态校验未通过）。",
                    *GetNameSafe(PC), *GetNameSafe(Chest));
      return;
    }
  }

  // 从另一个宝箱切换过来：把玩家从前一个宝箱的打开者集合里移除，
  // 否则前一个箱子会一直停在「打开」表现（它的 ActiveUsers 里还有这个玩家，
  // 而玩家已经无法再通过窗口关闭它）。
  if (PreviousContainer && PreviousContainer != Container) {
    if (AChestActor *PreviousChest =
            Cast<AChestActor>(PreviousContainer->GetOwner())) {
      WFLOG_INFO("[容器交互] 玩家 %s 从宝箱 %s 切换到 %s，关闭前一个宝箱。",
                 *GetNameSafe(PC), *GetNameSafe(PreviousChest),
                 *GetNameSafe(Container));
      PreviousChest->CloseChest(PC);
    }
  }

  WFLOG_INFO("[容器交互] 玩家 %s 打开容器 %s。", *GetNameSafe(PC),
             *GetNameSafe(Container));
  SetActiveContainer(Container);
}

bool UContainerInteractionComponent::Server_RequestCloseContainer_Validate(
    UItemContainer *Container) {
  // 允许传空：客户端只想关闭当前容器时不需要它。
  return true;
}

void UContainerInteractionComponent::
    Server_RequestCloseContainer_Implementation(UItemContainer *Container) {
  APlayerController *PC = GetOwningPlayerController();
  if (!PC) {
    WFLOG_WARNING("[容器交互] 关闭容器请求被拒：%s 没有 PlayerController。",
                  *GetNameSafe(GetOwner()));
    return;
  }
  if (PC->GetPawn() != GetOwner()) {
    WFLOG_WARNING(
        "[容器交互] 关闭容器请求被拒：%s 不是该 Controller 当前控制的 "
        "Pawn。",
        *GetNameSafe(GetOwner()));
    return;
  }

  UItemContainer *CurrentContainer = GetActiveContainer();
  if (!CurrentContainer) {
    WFLOG_INFO("[容器交互] %s 当前没有打开的容器，忽略关闭请求。",
               *GetNameSafe(GetOwner()));
    return;
  }

  if (Container && Container != CurrentContainer) {
    WFLOG_WARNING("[容器交互] %s 请求关闭容器 %s，但当前打开的是 %s；"
                  "仍按当前容器关闭。",
                  *GetNameSafe(GetOwner()), *GetNameSafe(Container),
                  *GetNameSafe(CurrentContainer));
  }

  // 宝箱：服务器统一记录「该玩家不再查看」，最后一名打开者离开时播放关箱
  // 表现（BP_OnChestClosed）。关闭不做权限检查——权限被撤销 / 玩家断线时
  // 也必须能把箱子关掉。
  if (AChestActor *Chest = Cast<AChestActor>(CurrentContainer->GetOwner())) {
    Chest->CloseChest(PC);
  }

  WFLOG_INFO("[容器交互] 玩家 %s 关闭容器 %s。", *GetNameSafe(PC),
             *GetNameSafe(CurrentContainer));
  SetActiveContainer(nullptr);
}

bool UContainerInteractionComponent::ValidateTransferRequest(
    UItemContainer *FromContainer, UItemContainer *ToContainer,
    const TCHAR *ActionName) const {
  APlayerController *PC = GetOwningPlayerController();
  if (!PC) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：%s 没有 PlayerController。",
                  ActionName, *GetNameSafe(GetOwner()));
    return false;
  }
  if (PC->GetPawn() != GetOwner()) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：%s 不是该 Controller 当前控制的 "
                  "Pawn。",
                  ActionName, *GetNameSafe(GetOwner()));
    return false;
  }
  if (!FromContainer || !ToContainer) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：源或目标容器为空。", ActionName);
    return false;
  }

  // 权限：源与目标都必须允许该玩家访问。客户端只传容器指针，物品数据由
  // 服务器从容器里读取；这里绝不能相信客户端传来的任何物品定义。
  if (!FromContainer->HasAccess(PC)) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：玩家 %s 无权访问源容器 %s"
                  "（权限模式=%d）。",
                  ActionName, *GetNameSafe(PC), *GetNameSafe(FromContainer),
                  static_cast<int32>(FromContainer->GetAccessMode()));
    return false;
  }
  if (!ToContainer->HasAccess(PC)) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：玩家 %s 无权访问目标容器 %s"
                  "（权限模式=%d）。",
                  ActionName, *GetNameSafe(PC), *GetNameSafe(ToContainer),
                  static_cast<int32>(ToContainer->GetAccessMode()));
    return false;
  }

  // 距离：玩家自己的容器永远通过；世界容器必须在交互距离内。
  if (!HasInteractionRange(FromContainer, PC)) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：源容器 %s 不在交互距离内。",
                  ActionName, *GetNameSafe(FromContainer));
    return false;
  }
  if (!HasInteractionRange(ToContainer, PC)) {
    WFLOG_WARNING("[容器交互] %s 请求被拒：目标容器 %s 不在交互距离内。",
                  ActionName, *GetNameSafe(ToContainer));
    return false;
  }

  return true;
}

bool UContainerInteractionComponent::Server_MoveItemBetweenContainers_Validate(
    UItemContainer *FromContainer, int32 FromSlot, UItemContainer *ToContainer,
    int32 ToSlot) {
  // 只做廉价参数检查（协议边界）；真实边界 / 权限 / 距离在实现里校验。
  // 槽位上限 4096 远大于当前任何容器容量（背包 30 / 快捷栏 10 / 箱子更小）。
  return FromContainer != nullptr && ToContainer != nullptr && FromSlot >= 0 &&
         FromSlot <= 4096 && ToSlot >= 0 && ToSlot <= 4096;
}

void UContainerInteractionComponent::
    Server_MoveItemBetweenContainers_Implementation(
        UItemContainer *FromContainer, int32 FromSlot,
        UItemContainer *ToContainer, int32 ToSlot) {
  if (!ValidateTransferRequest(FromContainer, ToContainer, TEXT("转移"))) {
    return;
  }

  APlayerController *PC = GetOwningPlayerController();

  // 真正的合并 / 交换规则由服务器上的容器决定。
  if (!FromContainer->MoveItemToContainer(FromSlot, ToContainer, ToSlot)) {
    WFLOG_WARNING("[容器交互] 玩家 %s 的转移请求未生效：%s[%d] -> %s[%d]"
                  "（槽位越界 / 源槽为空 / 目标无效）。",
                  *GetNameSafe(PC), *GetNameSafe(FromContainer), FromSlot,
                  *GetNameSafe(ToContainer), ToSlot);
    return;
  }

  WFLOG_INFO("[容器交互] 玩家 %s 完成转移：%s[%d] -> %s[%d]。",
             *GetNameSafe(PC), *GetNameSafe(FromContainer), FromSlot,
             *GetNameSafe(ToContainer), ToSlot);
}

bool UContainerInteractionComponent::Server_TransferAllFromContainer_Validate(
    UItemContainer *FromContainer, UItemContainer *ToContainer) {
  // 只做廉价参数检查；真实的权限 / 距离 / 容量判断在实现里做。
  return FromContainer != nullptr && ToContainer != nullptr;
}

void UContainerInteractionComponent::
    Server_TransferAllFromContainer_Implementation(
        UItemContainer *FromContainer, UItemContainer *ToContainer) {
  if (FromContainer == ToContainer) {
    WFLOG_WARNING("[容器交互] 全体转移请求被拒：源与目标是同一个容器 %s。",
                  *GetNameSafe(FromContainer));
    return;
  }

  if (!ValidateTransferRequest(FromContainer, ToContainer, TEXT("全体转移"))) {
    return;
  }

  APlayerController *PC = GetOwningPlayerController();
  const int32 Moved = FromContainer->TransferAllToContainer(ToContainer);

  if (Moved <= 0) {
    WFLOG_WARNING("[容器交互] 玩家 %s 的全体转移没有移动任何物品：%s -> %s"
                  "（目标已满且没有可合并的未满堆）。",
                  *GetNameSafe(PC), *GetNameSafe(FromContainer),
                  *GetNameSafe(ToContainer));
    return;
  }

  WFLOG_INFO("[容器交互] 玩家 %s 完成全体转移：%s -> %s，共 %d 个物品。",
             *GetNameSafe(PC), *GetNameSafe(FromContainer),
             *GetNameSafe(ToContainer), Moved);
}
