#pragma once

#include "CoreMinimal.h"

#include "Components/SlateWrapperTypes.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Templates/SubclassOf.h"
#include "UI/ToastMessage.h"

#include "WildforgeUIManager.generated.h"

class APlayerController;
class UToastWidget;
class UUserWidget;

/**
 * 界面展示判定函数：返回 true = 允许展示，false = 拒绝展示。
 *
 * 绑定在任意 UObject 的无参、返回 bool 的 UFUNCTION 上（推荐用
 * `UWildforgeUIManager::BindCanShowFunction`），也可以在蓝图里直接把委托传给
 * `BindCanShowDelegate`。每次 `ShowUI` 前都会调用它；没有绑定 = 永远允许展示。
 */
DECLARE_DYNAMIC_DELEGATE_RetVal(bool, FWildforgeCanShowUIDelegate);

/**
 * 管理器内部的「一个界面类」记录（实例 + 展示判定），由管理器独占维护，
 * 蓝图与其它 C++ 代码不需要直接使用。
 */
USTRUCT()
struct FWildforgeUIEntry {
  GENERATED_BODY()

  /** 这个条目对应的界面类（蓝图里就是选中的 WBP 类引用）。 */
  UPROPERTY(Transient)
  TSubclassOf<UUserWidget> WidgetClass;

  /** 已经创建的控件实例；为空 = 还没创建，或已被 RemoveUI 移除。 */
  UPROPERTY(Transient)
  TObjectPtr<UUserWidget> Widget = nullptr;

  /** 展示判定函数的绑定对象（弱引用，仅用于诊断「绑定对象已销毁」）。 */
  UPROPERTY(Transient)
  TWeakObjectPtr<UObject> CanShowOwner;

  /** 是否曾经成功绑定过展示判定函数（解绑 / 绑定对象失效后复位）。 */
  bool bHasCanShow = false;

  /** 展示判定函数本体（bHasCanShow 为 false 时不参与判断）。 */
  FWildforgeCanShowUIDelegate CanShowDelegate;

  /** 展示判定函数的 UFUNCTION 名（仅用于日志；委托方式绑定时为 None）。 */
  FName CanShowFunctionName = NAME_None;

  /** 首次创建时传给 AddToViewport 的层级。 */
  int32 ZOrder = 0;

  /** HideUI 收起之前的可见性，ShowUI 重新展示时恢复。 */
  ESlateVisibility RestoreVisibility = ESlateVisibility::Visible;
};

/**
 * 每个本地玩家一份的界面管理器（`ULocalPlayerSubsystem`）。
 *
 * ## 职责
 *
 * 用「界面类引用」统一管理屏幕上的界面：
 *
 *   - `ShowUI(类)`：按需创建控件并挂到视口；已经创建过就复用同一个实例；
 *   - `HideUI(类)`：暂时收起（保留实例与内部状态，下次 ShowUI 直接恢复）；
 *   - `RemoveUI(类)`：从视口移除并释放实例（下次 ShowUI 会重新创建）；
 *   - `IsUIShowing` / `GetUIWidget`：查询当前状态与实例。
 *
 * 每个界面类最多只有一个实例（同一个类重复 ShowUI 不会叠出两份）。
 *
 * ## 展示判定函数（能不能展示）
 *
 * 可以给任意界面类绑定一个「是否允许展示」的函数：
 *
 *   - `BindCanShowFunction(类, 函数所属对象, UFUNCTION 名)`：
 *     函数必须**无参、返回 bool**；名字或签名不对会在绑定时记 ERROR 并拒绝；
 *   - `BindCanShowDelegate(类, 委托)`：蓝图里已有委托时用这个；
 *   - `UnbindCanShow(类)`：解除限制（之后永远允许展示）。
 *
 * 每次 `ShowUI` 都会先调用判定函数：返回 false 时本次不展示并返回 nullptr（记一条
 * INFO）。**已在显示的界面不会因为判定函数变成 false 而被自动关掉**——关闭请显式调用
 * `HideUI` / `RemoveUI`。想在 UI 代码里提前询问，用 `CanShowUI(类)`。
 *
 * 判定函数通常绑在角色 / 控制器 / 组件上，例如「背包没满才能打开某个界面」。
 * 绑定对象被销毁时，判定函数会自动失效并按「允许展示」处理（记一条 WARNING 后解绑）。
 *
 * ## 提示（Toast）
 *
 * 不用自己创建 `UToastWidget`：`ShowToast` / `ShowToastStyled` / `ShowToastMessage`
 * 会在第一次调用时按 Project Settings > Game > **提示（Toast）** 里配置的控件类
 * 懒创建提示控件，之后复用同一个实例（并发提示由 `UToastWidget` 内部排队）。
 *
 * 想临时换一个提示皮肤，用 `SetToastWidgetClass` 覆盖（传空恢复配置类）；
 * 查询 / 清空用 `GetToastWidget` / `IsToastActive` / `ClearToasts`。
 *
 * ## 蓝图里怎么拿
 *
 * `Get UIManager`（World Context 传入 Self / 玩家控制器即可）拿到管理器，
 * 然后 `Show UI` 的类引用引脚选 WBP 类。纯客户端功能，不需要服务器参与，
 * 也不参与复制。
 *
 * ## 输入模式
 *
 * 本类**不**改输入模式（GameOnly / GameAndUI / UIOnly）与鼠标显隐：现有界面
 * （背包、宝箱窗口）各自管理自己的输入模式，管理器强行接管会与它们打架。
 * 需要时由调用方在 ShowUI / RemoveUI 前后自己设置。
 */
UCLASS(BlueprintType)
class WILDFORGE_API UWildforgeUIManager : public ULocalPlayerSubsystem {
  GENERATED_BODY()

public:
  /** 本地玩家销毁（换关卡 / 断线）时把所有界面从视口移除，避免残留在屏幕上。 */
  virtual void Deinitialize() override;

  /**
   * 拿到某个世界上下文（控制器 / 角色 / 控件都可以）所属本地玩家的界面管理器。
   *
   * 专用服务器没有本地玩家，返回 nullptr。
   *
   * 本项目按「单本地玩家」取第 0 个 PlayerController；将来做分屏时，
   * 调用方应改为从自己的 LocalPlayer 上 `GetSubsystem<UWildforgeUIManager>()`。
   */
  UFUNCTION(BlueprintPure, Category = "UI|Manager",
            meta = (WorldContext = "WorldContextObject"))
  static UWildforgeUIManager *GetUIManager(const UObject *WorldContextObject);

  /** 管理器所属的本地 PlayerController；还没建成 / 已销毁时为空。 */
  UFUNCTION(BlueprintPure, Category = "UI|Manager")
  APlayerController *GetOwningPlayerController() const;

  // ================= 展示 / 移除 =================

  /**
   * 展示一个界面（按类引用）。
   *
   * - 绑定过判定函数且返回 false：不展示，返回 nullptr；
   * - 已经创建过：复用实例（必要时重新挂回视口 / 恢复可见性），返回同一个控件；
   * - 还没创建：`CreateWidget` + `AddToViewport(ZOrder)`。
   *
   * `ZOrder` 只在**首次创建**时生效；已经创建的界面要换层级，请先 `RemoveUI`
   * 再 `ShowUI`（本引擎的 UMG 没有直接改视口层级的安全入口）。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager")
  UUserWidget *ShowUI(TSubclassOf<UUserWidget> WidgetClass, int32 ZOrder = 0);

  /**
   * 暂时收起一个界面：保留实例与内部状态，下次 ShowUI 直接恢复。
   *
   * 控件不在视口 / 没创建过时返回 false。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager")
  bool HideUI(TSubclassOf<UUserWidget> WidgetClass);

  /**
   * 从屏幕移除一个界面并释放实例（下次 ShowUI 会重新创建）。
   *
   * 只移除控件；该界面类上绑定的展示判定函数会保留，重新展示时仍然生效。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager")
  bool RemoveUI(TSubclassOf<UUserWidget> WidgetClass);

  /** 移除所有已创建的界面（保留每个类的展示判定绑定）。 */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager")
  void RemoveAllUI();

  /** 界面当前是否显示在屏幕上（在视口里且没有被 Collapsed / Hidden）。 */
  UFUNCTION(BlueprintPure, Category = "UI|Manager")
  bool IsUIShowing(TSubclassOf<UUserWidget> WidgetClass) const;

  /** 取已创建的控件实例（不保证正在显示）；没有时返回 nullptr。 */
  UFUNCTION(BlueprintPure, Category = "UI|Manager")
  UUserWidget *GetUIWidget(TSubclassOf<UUserWidget> WidgetClass) const;

  // ================= 展示判定 =================

  /**
   * 询问某个界面现在能不能展示（会调用已绑定的判定函数）。
   *
   * 没绑定判定函数时永远返回 true。只做查询，不改界面状态。
   */
  UFUNCTION(BlueprintPure, Category = "UI|Manager|CanShow")
  bool CanShowUI(TSubclassOf<UUserWidget> WidgetClass);

  /**
   * 给某个界面绑定展示判定函数。
   *
   * `FunctionOwner` 上必须有名为 `FunctionName` 的 UFUNCTION，且**无参数、返回 bool**；
   * 校验失败记 ERROR 并返回 false。绑定对象是弱引用：对象销毁后判定自动失效
   * （下一次 ShowUI 时记 WARNING、解除绑定并按允许展示处理）。
   *
   * 同一个界面类重复绑定会覆盖旧函数。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|CanShow")
  bool BindCanShowFunction(TSubclassOf<UUserWidget> WidgetClass,
                           UObject *FunctionOwner, FName FunctionName);

  /**
   * 用现成的委托绑定展示判定函数（蓝图里已有事件 / 委托时使用）。
   *
   * 委托必须已经绑定到有效对象，否则记 WARNING 并返回 false（空委托不等于
   * 「永远允许」——后者请用 `UnbindCanShow`）。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|CanShow")
  bool BindCanShowDelegate(TSubclassOf<UUserWidget> WidgetClass,
                           FWildforgeCanShowUIDelegate CanShowDelegate);

  /** 解除某个界面的展示判定（之后永远允许展示）。没有绑定过时返回 false。 */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|CanShow")
  bool UnbindCanShow(TSubclassOf<UUserWidget> WidgetClass);

  // ================= 提示（Toast） =================

  /**
   * 弹出一条纯文本提示（颜色 / 图标 / 时长都用 WBP 类默认值）。
   *
   * 第一次调用会按 Project Settings 里的配置懒创建提示控件并挂到视口，
   * 之后复用同一个实例；同时到达的提示由 `UToastWidget` 内部排队。
   *
   * 返回接收这条提示的控件；未配置控件类 / 加载失败 / 展示判定拒绝时返回 nullptr。
   * 文本为空时记 WARNING 并直接返回 nullptr（不会创建控件）。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|Toast")
  UToastWidget *ShowToast(const FText &Message);

  /**
   * 弹出一条指定文本颜色、背景颜色与停留时长的提示（图标仍用 WBP 默认值）。
   *
   * `Duration <= 0` 表示使用 WBP 上的 `DefaultDuration`。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|Toast")
  UToastWidget *ShowToastStyled(const FText &Message, FLinearColor TextColor,
                                FLinearColor BackgroundColor,
                                float Duration = 0.0f);

  /**
   * 用「警告」样式弹一条提示：红色文本 + 深红背景，默认停留 4 秒。
   *
   * 用于「精力不足」这类需要立刻被注意到的反馈。颜色是纯客户端表现
   * （不参与玩法判定），需要换配色时改 `ShowToastWarning` 的实现或用
   * `ShowToastMessage` 逐条指定。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|Toast")
  UToastWidget *ShowToastWarning(const FText &Message, float Duration = 4.0f);

  /** 完整数据入口：文本 / 颜色 / 图标 / 停留时长逐条指定。 */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|Toast")
  UToastWidget *ShowToastMessage(const FToastMessage &Message);

  /**
   * 清空提示（含待显示队列）。
   *
   * @param bClearActive true = 当前正在显示的提示也立即收起；false = 让它自然播完。
   * @return 提示控件尚未创建过时返回 false。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|Toast")
  bool ClearToasts(bool bClearActive = true);

  /** 当前是否有提示正在显示（含淡入 / 停留 / 淡出阶段）。 */
  UFUNCTION(BlueprintPure, Category = "UI|Manager|Toast")
  bool IsToastActive();

  /** 取当前的提示控件（**不会**懒创建）；还没创建过时返回 nullptr。 */
  UFUNCTION(BlueprintPure, Category = "UI|Manager|Toast")
  UToastWidget *GetToastWidget();

  /**
   * 覆盖提示控件类（默认使用 Project Settings > Game > 提示（Toast）里的配置）。
   *
   * 传空 = 清除运行时覆盖、恢复使用配置类。已有提示控件时换类会先移除旧实例
   *（正在显示的提示与待显示队列会一起丢失）。类无效 / 是 Abstract 时返回 false。
   */
  UFUNCTION(BlueprintCallable, Category = "UI|Manager|Toast")
  bool SetToastWidgetClass(TSubclassOf<UToastWidget> InToastWidgetClass);

  /** 当前生效的提示控件类；没有配置 / 加载失败时返回空。 */
  UFUNCTION(BlueprintPure, Category = "UI|Manager|Toast")
  TSubclassOf<UToastWidget> GetToastWidgetClass();

private:
  /** 找到该类对应的条目下标；没有返回 INDEX_NONE。 */
  int32 FindEntryIndex(const TSubclassOf<UUserWidget> &WidgetClass) const;

  /** 找到（或新建）该类对应的条目下标。 */
  int32 FindOrAddEntryIndex(const TSubclassOf<UUserWidget> &WidgetClass);

  /**
   * 调用条目的判定函数：未绑定 = true。
   *
   * 绑定对象已销毁（委托失效）时记一条 WARNING、解除绑定并按允许处理，
   * 避免一个死引用永久挡住界面（也避免每次查询都刷日志）。
   */
  bool EvaluateCanShow(FWildforgeUIEntry &Entry);

  /**
   * 解析当前应使用的提示控件类：运行时覆盖优先，否则读 `UToastSettings` 并缓存。
   *
   * 配置缺失 / 加载失败 / 类不合法时记 ERROR 并返回空；失败结果也缓存，
   * 避免每次弹提示都重复加载与刷日志。
   */
  TSubclassOf<UToastWidget> ResolveToastWidgetClass();

  /** 取提示控件，必要时懒创建；创建失败（未配置 / 判定拒绝等）返回 nullptr。 */
  UToastWidget *EnsureToastWidget();

  /** 读取提示控件的视口层级（配置值，负值按 0 处理）。 */
  int32 ResolveToastZOrder() const;

  /** 每个界面类一条记录（数量很小，线性查找即可）。 */
  UPROPERTY(Transient)
  TArray<FWildforgeUIEntry> Entries;

  /** SetToastWidgetClass 设置的运行时覆盖；为空表示使用配置类。 */
  UPROPERTY(Transient)
  TSubclassOf<UToastWidget> ToastWidgetClassOverride;

  /** 从配置解析出的提示控件类（缓存，避免重复 LoadSynchronous）。 */
  UPROPERTY(Transient)
  TSubclassOf<UToastWidget> ResolvedToastWidgetClass;

  /** 是否已经尝试过从配置解析（失败也置 true，避免每次调用都刷日志）。 */
  bool bTriedResolveToastWidgetClass = false;
};
