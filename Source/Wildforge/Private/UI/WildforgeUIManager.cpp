#include "UI/WildforgeUIManager.h"

#include "Blueprint/UserWidget.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Settings/ToastSettings.h"
#include "UI/ToastWidget.h"
#include "UObject/UnrealType.h"
#include "Utils/WildforgeLog.h"

// 界面管理器的落地约定（与 CLAUDE.md 的强制约束对照）：
//
//   - 纯客户端：管理器是 ULocalPlayerSubsystem，只在有本地玩家的端存在，
//     不参与复制、没有 RPC，也不读 ini；
//   - 按「类引用」管理：每个界面类最多一个实例，类本身就是键；
//   - 展示判定函数只在 ShowUI 前调用；已显示的界面不会因为判定变 false 被自动关掉；
//   - 输入模式（GameOnly / GameAndUI / UIOnly）不由本类接管：背包、宝箱窗口各自管理
//     自己的输入模式（见 bug-121 的恢复规则），管理器强行接管会与它们打架。

// ================= 生命周期 =================

void UWildforgeUIManager::Deinitialize() {
  // LocalPlayer 销毁（换关卡 / 断线）时，AddToViewport 的控件不会自动消失，
  // 不主动移除的话会残留在屏幕上。展示判定绑定随子系统一起销毁，无需处理。
  RemoveAllUI();
  WFLOG_INFO("UIManager::Deinitialize: 本地玩家销毁，已清理界面（%s）",
             *GetNameSafe(this));

  Super::Deinitialize();
}

// ================= 获取 =================

UWildforgeUIManager *
UWildforgeUIManager::GetUIManager(const UObject *WorldContextObject) {
  if (!WorldContextObject) {
    WFLOG_ERROR("UIManager::GetUIManager: WorldContextObject 为空，"
                "蓝图里请把 Self / 玩家控制器接到 World Context 引脚");
    return nullptr;
  }

  APlayerController *PlayerController =
      UGameplayStatics::GetPlayerController(WorldContextObject, 0);

  // 专用服务器没有本地玩家（本地 PlayerController 也不存在），这里返回空是
  // 正常状态：本类只服务本机屏幕上的 UI。
  return ULocalPlayer::GetSubsystemFromController<UWildforgeUIManager>(
      PlayerController);
}

APlayerController *UWildforgeUIManager::GetOwningPlayerController() const {
  if (ULocalPlayer *LocalPlayer = GetLocalPlayer()) {
    // ULocalPlayer 上的版本是 UPlayer::GetPlayerController(const UWorld*)：
    // 传入本端世界时按 LocalPlayer 匹配当前世界的控制器（换关卡后不会拿到旧 PC）。
    return LocalPlayer->GetPlayerController(GetWorld());
  }
  return nullptr;
}

// ================= 展示 / 移除 =================

UUserWidget *UWildforgeUIManager::ShowUI(
    TSubclassOf<UUserWidget> WidgetClass, int32 ZOrder) {
  if (!WidgetClass) {
    WFLOG_ERROR("UIManager::ShowUI: WidgetClass 为空（蓝图里没有选界面类），"
                "忽略本次请求");
    return nullptr;
  }

  if (WidgetClass->HasAnyClassFlags(CLASS_Abstract)) {
    WFLOG_ERROR("UIManager::ShowUI: 界面类 %s 是 Abstract，不能直接实例化；"
                "请在蓝图里继承它并选择对应的 WBP 子类",
                *WidgetClass->GetName());
    return nullptr;
  }

  // 判定函数在校验「要不要创建 / 重新展示」之前调用：每次 ShowUI 都会问一次。
  int32 Index = FindEntryIndex(WidgetClass);
  if (Index != INDEX_NONE && !EvaluateCanShow(Entries[Index])) {
    const FName FunctionName = Entries[Index].CanShowFunctionName;
    WFLOG_INFO("UIManager::ShowUI: 界面 %s 的展示判定函数 %s 返回 false，"
               "本次不展示（已显示的实例保持原状）",
               *WidgetClass->GetName(),
               FunctionName.IsNone() ? TEXT("<委托>")
                                     : *FunctionName.ToString());
    return nullptr;
  }

  Index = FindOrAddEntryIndex(WidgetClass);
  FWildforgeUIEntry &Entry = Entries[Index];

  // ---- 已经创建过：复用实例 ----
  if (IsValid(Entry.Widget)) {
    UUserWidget *ExistingWidget = Entry.Widget;

    if (!ExistingWidget->IsInViewport()) {
      ExistingWidget->AddToViewport(Entry.ZOrder);
      WFLOG_INFO("UIManager::ShowUI: 界面 %s 的实例还在，但不在视口里，"
                 "已重新挂回（ZOrder=%d）",
                 *WidgetClass->GetName(), Entry.ZOrder);
    }

    const ESlateVisibility CurrentVisibility = ExistingWidget->GetVisibility();
    if (CurrentVisibility == ESlateVisibility::Collapsed ||
        CurrentVisibility == ESlateVisibility::Hidden) {
      ExistingWidget->SetVisibility(Entry.RestoreVisibility);
      WFLOG_INFO("UIManager::ShowUI: 界面 %s 从收起状态恢复显示（可见性 %d）",
                 *WidgetClass->GetName(),
                 static_cast<int32>(Entry.RestoreVisibility));
    }

    if (ZOrder != Entry.ZOrder) {
      // UE 5.7 的 UMG 没有直接修改视口层级的入口（UUserWidget 上没有
      // SetZOrderInViewport），所以这里只提示，不改层级。
      WFLOG_INFO("UIManager::ShowUI: 界面 %s 已经创建，ZOrder 只在首次创建时生效"
                 "（当前 %d，本次请求 %d）；要换层级请先 RemoveUI 再 ShowUI",
                 *WidgetClass->GetName(), Entry.ZOrder, ZOrder);
    }

    return ExistingWidget;
  }

  // ---- 首次创建 ----
  APlayerController *PlayerController = GetOwningPlayerController();
  if (!PlayerController) {
    WFLOG_ERROR("UIManager::ShowUI: 拿不到本地 PlayerController，无法创建界面 %s"
                "（专用服务器没有本地 UI；客户端请等 Pawn / Controller 就绪后再调用）",
                *WidgetClass->GetName());
    return nullptr;
  }

  UUserWidget *Widget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
  if (!Widget) {
    WFLOG_ERROR("UIManager::ShowUI: CreateWidget 失败（类 %s）。"
                "检查它是否是可实例化的 WBP、是否有 WidgetTree",
                *WidgetClass->GetName());
    return nullptr;
  }

  // CreateWidget 会跑控件的构造 / 初始化逻辑，期间若有人再次调用本管理器，
  // Entries 可能已经扩容；条目只增不删，所以下标仍然有效，重新取一次引用即可。
  FWildforgeUIEntry &EntryAfterCreate = Entries[Index];

  // 记录「展示时应恢复成什么可见性」：WBP 里默认 Collapsed / Hidden 的控件
  // 被 ShowUI 调用时应该真的显示出来，其余（Visible / HitTestInvisible…）原样保留。
  const ESlateVisibility InitialVisibility = Widget->GetVisibility();
  EntryAfterCreate.RestoreVisibility =
      (InitialVisibility == ESlateVisibility::Collapsed ||
       InitialVisibility == ESlateVisibility::Hidden)
          ? ESlateVisibility::Visible
          : InitialVisibility;
  EntryAfterCreate.Widget = Widget;
  EntryAfterCreate.ZOrder = ZOrder;

  Widget->AddToViewport(ZOrder);
  Widget->SetVisibility(EntryAfterCreate.RestoreVisibility);

  WFLOG_INFO("UIManager::ShowUI: 创建并展示界面 %s（ZOrder=%d，OwningPlayer=%s）",
             *WidgetClass->GetName(), ZOrder, *PlayerController->GetName());
  return Widget;
}

bool UWildforgeUIManager::HideUI(TSubclassOf<UUserWidget> WidgetClass) {
  const int32 Index = FindEntryIndex(WidgetClass);
  if (Index == INDEX_NONE || !IsValid(Entries[Index].Widget)) {
    WFLOG_INFO("UIManager::HideUI: 界面 %s 还没有创建，无需收起",
               *GetNameSafe(WidgetClass.Get()));
    return false;
  }

  FWildforgeUIEntry &Entry = Entries[Index];
  UUserWidget *Widget = Entry.Widget;

  // 记录收起前的可见性：重复 HideUI 时不要把 Collapsed 记成恢复值。
  const ESlateVisibility CurrentVisibility = Widget->GetVisibility();
  if (CurrentVisibility != ESlateVisibility::Collapsed &&
      CurrentVisibility != ESlateVisibility::Hidden) {
    Entry.RestoreVisibility = CurrentVisibility;
  }

  Widget->SetVisibility(ESlateVisibility::Collapsed);
  WFLOG_INFO("UIManager::HideUI: 界面 %s 已收起（实例保留，下次 ShowUI 直接恢复）",
             *WidgetClass->GetName());
  return true;
}

bool UWildforgeUIManager::RemoveUI(TSubclassOf<UUserWidget> WidgetClass) {
  const int32 Index = FindEntryIndex(WidgetClass);
  if (Index == INDEX_NONE || !IsValid(Entries[Index].Widget)) {
    WFLOG_INFO("UIManager::RemoveUI: 界面 %s 当前没有实例，无需移除",
               *GetNameSafe(WidgetClass.Get()));
    return false;
  }

  FWildforgeUIEntry &Entry = Entries[Index];
  UUserWidget *Widget = Entry.Widget;

  Widget->RemoveFromParent();
  Entry.Widget = nullptr;

  WFLOG_INFO("UIManager::RemoveUI: 界面 %s 已从视口移除并释放实例"
             "（该类上的展示判定绑定继续保留，重新 ShowUI 时仍然生效）",
             *WidgetClass->GetName());
  return true;
}

void UWildforgeUIManager::RemoveAllUI() {
  int32 RemovedCount = 0;
  for (FWildforgeUIEntry &Entry : Entries) {
    if (!IsValid(Entry.Widget)) {
      Entry.Widget = nullptr;
      continue;
    }

    Entry.Widget->RemoveFromParent();
    Entry.Widget = nullptr;
    ++RemovedCount;
  }

  WFLOG_INFO("UIManager::RemoveAllUI: 已移除 %d 个界面（%d 条类级记录与判定绑定保留）",
             RemovedCount, Entries.Num());
}

bool UWildforgeUIManager::IsUIShowing(
    TSubclassOf<UUserWidget> WidgetClass) const {
  const int32 Index = FindEntryIndex(WidgetClass);
  if (Index == INDEX_NONE) {
    return false;
  }

  const UUserWidget *Widget = Entries[Index].Widget;
  if (!IsValid(Widget) || !Widget->IsInViewport()) {
    return false;
  }

  const ESlateVisibility Visibility = Widget->GetVisibility();
  return Visibility != ESlateVisibility::Collapsed &&
         Visibility != ESlateVisibility::Hidden;
}

UUserWidget *
UWildforgeUIManager::GetUIWidget(TSubclassOf<UUserWidget> WidgetClass) const {
  const int32 Index = FindEntryIndex(WidgetClass);
  if (Index == INDEX_NONE) {
    return nullptr;
  }

  UUserWidget *Widget = Entries[Index].Widget;
  return IsValid(Widget) ? Widget : nullptr;
}

// ================= 展示判定 =================

bool UWildforgeUIManager::CanShowUI(TSubclassOf<UUserWidget> WidgetClass) {
  if (!WidgetClass) {
    return false;
  }

  const int32 Index = FindEntryIndex(WidgetClass);
  if (Index == INDEX_NONE) {
    // 没有登记过 = 没有绑定判定函数 = 允许展示。
    return true;
  }

  return EvaluateCanShow(Entries[Index]);
}

bool UWildforgeUIManager::BindCanShowFunction(
    TSubclassOf<UUserWidget> WidgetClass, UObject *FunctionOwner,
    FName FunctionName) {
  if (!WidgetClass) {
    WFLOG_ERROR("UIManager::BindCanShowFunction: WidgetClass 为空，忽略本次绑定");
    return false;
  }

  if (!IsValid(FunctionOwner)) {
    WFLOG_ERROR("UIManager::BindCanShowFunction: 界面 %s 的判定函数所属对象无效"
                "（空指针或已销毁），忽略本次绑定",
                *WidgetClass->GetName());
    return false;
  }

  if (FunctionName.IsNone()) {
    WFLOG_ERROR("UIManager::BindCanShowFunction: 界面 %s 的判定函数名为空，"
                "请在蓝图里填写对象上的 UFUNCTION 名",
                *WidgetClass->GetName());
    return false;
  }

  UFunction *Function = FunctionOwner->FindFunction(FunctionName);
  if (!Function) {
    WFLOG_ERROR("UIManager::BindCanShowFunction: %s 上找不到 UFUNCTION '%s'。"
                "函数必须标记为 UFUNCTION，且名字逐字一致（区分大小写）",
                *GetNameSafe(FunctionOwner), *FunctionName.ToString());
    return false;
  }

  // 判定函数必须「无参 + 返回 bool」：ShowUI 只回答「能不能展示」这一个问题。
  // UHT 不校验委托绑定对象的签名，签名不对时引擎要到执行那一刻才报错，
  // 所以这里在绑定入口就查清楚。
  int32 InputParamCount = 0;
  bool bReturnsBool = false;
  for (TFieldIterator<FProperty> It(Function);
       It && (It->PropertyFlags & CPF_Parm); ++It) {
    if (It->PropertyFlags & CPF_ReturnParm) {
      bReturnsBool = It->IsA<FBoolProperty>();
    } else {
      ++InputParamCount;
    }
  }

  if (InputParamCount != 0 || !bReturnsBool) {
    WFLOG_ERROR("UIManager::BindCanShowFunction: %s.%s 的签名不对"
                "（参数个数 %d，返回 bool = %s）。判定函数必须无参并返回 bool",
                *GetNameSafe(FunctionOwner), *FunctionName.ToString(),
                InputParamCount, bReturnsBool ? TEXT("是") : TEXT("否"));
    return false;
  }

  const int32 Index = FindOrAddEntryIndex(WidgetClass);
  FWildforgeUIEntry &Entry = Entries[Index];

  // 同一个类重复绑定 = 覆盖旧函数，避免旧限制悄悄继续生效。
  if (Entry.bHasCanShow &&
      !(Entry.CanShowFunctionName == FunctionName &&
        Entry.CanShowOwner.Get() == FunctionOwner)) {
    WFLOG_INFO("UIManager::BindCanShowFunction: 界面 %s 原有判定函数 %s 将被覆盖",
               *WidgetClass->GetName(),
               Entry.CanShowFunctionName.IsNone()
                   ? TEXT("<委托>")
                   : *Entry.CanShowFunctionName.ToString());
  }

  Entry.CanShowDelegate.BindUFunction(FunctionOwner, FunctionName);
  Entry.CanShowOwner = FunctionOwner;
  Entry.CanShowFunctionName = FunctionName;
  Entry.bHasCanShow = true;

  WFLOG_INFO("UIManager::BindCanShowFunction: 界面 %s 已绑定展示判定函数 %s.%s"
             "（每次 ShowUI 前调用，返回 false 则不展示）",
             *WidgetClass->GetName(), *GetNameSafe(FunctionOwner),
             *FunctionName.ToString());
  return true;
}

bool UWildforgeUIManager::BindCanShowDelegate(
    TSubclassOf<UUserWidget> WidgetClass,
    FWildforgeCanShowUIDelegate CanShowDelegate) {
  if (!WidgetClass) {
    WFLOG_ERROR("UIManager::BindCanShowDelegate: WidgetClass 为空，忽略本次绑定");
    return false;
  }

  if (!CanShowDelegate.IsBound()) {
    WFLOG_WARNING("UIManager::BindCanShowDelegate: 界面 %s 收到的委托没有绑定"
                  "任何对象 / 函数，忽略本次绑定"
                  "（要表示「永远允许展示」请用 UnbindCanShow）",
                  *WidgetClass->GetName());
    return false;
  }

  const int32 Index = FindOrAddEntryIndex(WidgetClass);
  FWildforgeUIEntry &Entry = Entries[Index];

  Entry.CanShowDelegate = CanShowDelegate;
  Entry.CanShowOwner = CanShowDelegate.GetUObject();
  Entry.CanShowFunctionName = NAME_None;
  Entry.bHasCanShow = true;

  WFLOG_INFO("UIManager::BindCanShowDelegate: 界面 %s 已绑定展示判定委托（对象 %s）",
             *WidgetClass->GetName(),
             *GetNameSafe(CanShowDelegate.GetUObject()));
  return true;
}

bool UWildforgeUIManager::UnbindCanShow(TSubclassOf<UUserWidget> WidgetClass) {
  const int32 Index = FindEntryIndex(WidgetClass);
  if (Index == INDEX_NONE || !Entries[Index].bHasCanShow) {
    WFLOG_INFO("UIManager::UnbindCanShow: 界面 %s 没有绑定展示判定函数，无需解绑",
               *GetNameSafe(WidgetClass.Get()));
    return false;
  }

  FWildforgeUIEntry &Entry = Entries[Index];
  const FName OldFunctionName = Entry.CanShowFunctionName;

  Entry.CanShowDelegate.Unbind();
  Entry.CanShowOwner.Reset();
  Entry.CanShowFunctionName = NAME_None;
  Entry.bHasCanShow = false;

  WFLOG_INFO("UIManager::UnbindCanShow: 界面 %s 已解除展示判定函数 %s"
             "（之后永远允许展示）",
             *WidgetClass->GetName(),
             OldFunctionName.IsNone() ? TEXT("<委托>")
                                      : *OldFunctionName.ToString());
  return true;
}

// ================= 提示（Toast） =================

UToastWidget *UWildforgeUIManager::ShowToast(const FText &Message) {
  if (Message.IsEmpty()) {
    WFLOG_WARNING("UIManager::ShowToast: 收到空文本提示，已忽略（不会创建提示控件）");
    return nullptr;
  }

  UToastWidget *ToastWidget = EnsureToastWidget();
  if (!ToastWidget) {
    return nullptr;
  }

  ToastWidget->ShowToast(Message);
  return ToastWidget;
}

UToastWidget *UWildforgeUIManager::ShowToastStyled(const FText &Message,
                                                   FLinearColor TextColor,
                                                   FLinearColor BackgroundColor,
                                                   float Duration) {
  if (Message.IsEmpty()) {
    WFLOG_WARNING("UIManager::ShowToastStyled: 收到空文本提示，已忽略"
                  "（不会创建提示控件）");
    return nullptr;
  }

  UToastWidget *ToastWidget = EnsureToastWidget();
  if (!ToastWidget) {
    return nullptr;
  }

  ToastWidget->ShowToastStyled(Message, TextColor, BackgroundColor, Duration);
  return ToastWidget;
}

UToastWidget *UWildforgeUIManager::ShowToastWarning(const FText &Message,
                                                    float Duration) {
  // 警告样式是纯客户端表现（与 WBP 皮肤同类），不进 ini：
  // 红字 + 深红底，保证在亮色场景里也能一眼看到。
  static const FLinearColor WarningTextColor(1.0f, 0.25f, 0.2f, 1.0f);
  static const FLinearColor WarningBackgroundColor(0.35f, 0.02f, 0.02f, 0.9f);

  return ShowToastStyled(Message, WarningTextColor, WarningBackgroundColor,
                         Duration);
}

UToastWidget *
UWildforgeUIManager::ShowToastMessage(const FToastMessage &Message) {
  if (Message.Message.IsEmpty()) {
    WFLOG_WARNING("UIManager::ShowToastMessage: 收到空文本提示，已忽略"
                  "（不会创建提示控件）");
    return nullptr;
  }

  UToastWidget *ToastWidget = EnsureToastWidget();
  if (!ToastWidget) {
    return nullptr;
  }

  ToastWidget->ShowToastMessage(Message);
  return ToastWidget;
}

bool UWildforgeUIManager::ClearToasts(bool bClearActive) {
  UToastWidget *ToastWidget = GetToastWidget();
  if (!ToastWidget) {
    WFLOG_INFO("UIManager::ClearToasts: 提示控件尚未创建，无需清空");
    return false;
  }

  ToastWidget->ClearToasts(bClearActive);
  return true;
}

bool UWildforgeUIManager::IsToastActive() {
  const UToastWidget *ToastWidget = GetToastWidget();
  return ToastWidget && ToastWidget->IsToastActive();
}

UToastWidget *UWildforgeUIManager::GetToastWidget() {
  const TSubclassOf<UToastWidget> ToastClass = ResolveToastWidgetClass();
  if (!ToastClass) {
    return nullptr;
  }

  return Cast<UToastWidget>(GetUIWidget(ToastClass));
}

bool UWildforgeUIManager::SetToastWidgetClass(
    TSubclassOf<UToastWidget> InToastWidgetClass) {
  if (!InToastWidgetClass) {
    ToastWidgetClassOverride = nullptr;
    ResolvedToastWidgetClass = nullptr;
    bTriedResolveToastWidgetClass = false;

    WFLOG_INFO("UIManager::SetToastWidgetClass: 已清除运行时覆盖，"
               "恢复使用 Project Settings 中的提示控件类");
    return true;
  }

  if (InToastWidgetClass->HasAnyClassFlags(CLASS_Abstract)) {
    WFLOG_ERROR("UIManager::SetToastWidgetClass: %s 是 Abstract，不能实例化；"
                "请选它的 WBP 子类",
                *InToastWidgetClass->GetName());
    return false;
  }

  if (!InToastWidgetClass->IsChildOf(UToastWidget::StaticClass())) {
    WFLOG_ERROR("UIManager::SetToastWidgetClass: %s 不是 UToastWidget 的子类，"
                "无法作为提示控件",
                *InToastWidgetClass->GetName());
    return false;
  }

  if (ToastWidgetClassOverride == InToastWidgetClass) {
    WFLOG_INFO("UIManager::SetToastWidgetClass: 提示控件类已经是 %s，无需重复设置",
               *InToastWidgetClass->GetName());
    return true;
  }

  // 换皮肤前先清掉旧实例：否则屏幕上会同时存在两套提示控件，
  // 旧实例里的待显示队列也不会再被消费。
  if (UToastWidget *OldToastWidget = GetToastWidget()) {
    WFLOG_INFO("UIManager::SetToastWidgetClass: 换类前移除旧提示控件 %s"
               "（其正在显示与排队中的提示会丢失）",
               *OldToastWidget->GetClass()->GetName());
    RemoveUI(OldToastWidget->GetClass());
  }

  ToastWidgetClassOverride = InToastWidgetClass;
  ResolvedToastWidgetClass = nullptr;
  bTriedResolveToastWidgetClass = false;

  WFLOG_INFO("UIManager::SetToastWidgetClass: 提示控件类已覆盖为 %s",
             *InToastWidgetClass->GetName());
  return true;
}

TSubclassOf<UToastWidget> UWildforgeUIManager::GetToastWidgetClass() {
  return ResolveToastWidgetClass();
}

// ================= 内部 =================

bool UWildforgeUIManager::EvaluateCanShow(FWildforgeUIEntry &Entry) {
  if (!Entry.bHasCanShow) {
    return true;
  }

  if (!Entry.CanShowDelegate.IsBound()) {
    // 绑定对象被销毁（换关卡 / Actor 被删）后，委托会失效。这属于运行时正常现象，
    // 不能让它永久挡住界面；记一条 WARNING 后解除绑定（只发生一次，不刷日志）。
    WFLOG_WARNING("UIManager: 界面 %s 的展示判定函数 %s 已失效"
                  "（绑定对象可能已销毁），已自动解除绑定并按允许展示处理",
                  *GetNameSafe(Entry.WidgetClass.Get()),
                  Entry.CanShowFunctionName.IsNone()
                      ? TEXT("<委托>")
                      : *Entry.CanShowFunctionName.ToString());

    Entry.CanShowDelegate.Unbind();
    Entry.CanShowOwner.Reset();
    Entry.CanShowFunctionName = NAME_None;
    Entry.bHasCanShow = false;
    return true;
  }

  return Entry.CanShowDelegate.Execute();
}

TSubclassOf<UToastWidget> UWildforgeUIManager::ResolveToastWidgetClass() {
  // 运行时覆盖优先：允许蓝图在特定玩法流程里临时换提示皮肤。
  if (ToastWidgetClassOverride) {
    return ToastWidgetClassOverride;
  }

  if (bTriedResolveToastWidgetClass) {
    return ResolvedToastWidgetClass;
  }

  // 失败也标记为「已尝试」，避免每次弹提示都重复加载资产 / 刷同一条 ERROR。
  bTriedResolveToastWidgetClass = true;
  ResolvedToastWidgetClass = nullptr;

  const UToastSettings *Settings = UToastSettings::Get();
  if (!Settings || Settings->ToastWidgetClass.IsNull()) {
    WFLOG_ERROR("UIManager: 没有配置提示控件类。请在 Project Settings > Game > "
                "提示（Toast）里把 ToastWidgetClass 指到一个 UToastWidget 的 "
                "WBP 子类（不能选 Abstract 的原生类）");
    return nullptr;
  }

  UClass *LoadedClass = Settings->ToastWidgetClass.LoadSynchronous();
  if (!LoadedClass) {
    WFLOG_ERROR("UIManager: 提示控件类加载失败：%s。"
                "检查 ini 里的路径是否指向存在的 WBP 资产",
                *Settings->ToastWidgetClass.ToString());
    return nullptr;
  }

  if (LoadedClass->HasAnyClassFlags(CLASS_Abstract)) {
    WFLOG_ERROR("UIManager: 配置的提示控件类 %s 是 Abstract，不能实例化；"
                "请改选它的 WBP 子类",
                *LoadedClass->GetName());
    return nullptr;
  }

  if (!LoadedClass->IsChildOf(UToastWidget::StaticClass())) {
    WFLOG_ERROR("UIManager: 配置的提示控件类 %s 不是 UToastWidget 的子类",
                *LoadedClass->GetName());
    return nullptr;
  }

  ResolvedToastWidgetClass = LoadedClass;
  WFLOG_INFO("UIManager: 提示控件类解析成功：%s", *LoadedClass->GetName());
  return ResolvedToastWidgetClass;
}

UToastWidget *UWildforgeUIManager::EnsureToastWidget() {
  const TSubclassOf<UToastWidget> ToastClass = ResolveToastWidgetClass();
  if (!ToastClass) {
    return nullptr;
  }

  // 已经创建过：直接复用。Toast 空闲时会把自己 Collapsed，不能走 ShowUI
  // 的「从收起状态恢复」分支（那会每弹一条都记一次 INFO，还会短暂显示空控件）。
  if (UToastWidget *ExistingToast = Cast<UToastWidget>(GetUIWidget(ToastClass))) {
    if (ExistingToast->IsInViewport()) {
      return ExistingToast;
    }

    // 被外部移出视口（不是通过管理器 RemoveUI）时补挂回去；
    // 走 ShowUI 以保持 Entries 的状态一致，也遵守可能存在的展示判定。
    UUserWidget *ReAdded = ShowUI(ToastClass, ResolveToastZOrder());
    UToastWidget *ReAddedToast = Cast<UToastWidget>(ReAdded);
    if (!ReAddedToast) {
      WFLOG_ERROR("UIManager: 提示控件 %s 不在视口里，重新挂回失败",
                  *ToastClass->GetName());
      return nullptr;
    }
    return ReAddedToast;
  }

  const int32 ToastZOrder = ResolveToastZOrder();
  UUserWidget *CreatedWidget = ShowUI(ToastClass, ToastZOrder);
  UToastWidget *CreatedToast = Cast<UToastWidget>(CreatedWidget);
  if (!CreatedToast) {
    // ShowUI 对「判定拒绝 / 创建失败 / 没有本地 PC」已经记过日志，这里不重复。
    return nullptr;
  }

  WFLOG_INFO("UIManager: 已创建提示控件 %s（ZOrder=%d）",
             *ToastClass->GetName(), ToastZOrder);
  return CreatedToast;
}

int32 UWildforgeUIManager::ResolveToastZOrder() const {
  const UToastSettings *Settings = UToastSettings::Get();
  const int32 ConfiguredZOrder = Settings ? Settings->ZOrder : 100;
  if (ConfiguredZOrder < 0) {
    WFLOG_WARNING("UIManager: 提示 ZOrder 配置为 %d（负值），按 0 处理",
                  ConfiguredZOrder);
    return 0;
  }
  return ConfiguredZOrder;
}

int32 UWildforgeUIManager::FindEntryIndex(
    const TSubclassOf<UUserWidget> &WidgetClass) const {
  for (int32 Index = 0; Index < Entries.Num(); ++Index) {
    if (Entries[Index].WidgetClass == WidgetClass) {
      return Index;
    }
  }
  return INDEX_NONE;
}

int32 UWildforgeUIManager::FindOrAddEntryIndex(
    const TSubclassOf<UUserWidget> &WidgetClass) {
  const int32 ExistingIndex = FindEntryIndex(WidgetClass);
  if (ExistingIndex != INDEX_NONE) {
    return ExistingIndex;
  }

  FWildforgeUIEntry &NewEntry = Entries.AddDefaulted_GetRef();
  NewEntry.WidgetClass = WidgetClass;
  return Entries.Num() - 1;
}
