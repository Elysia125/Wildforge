#pragma once

#include "CoreMinimal.h"

#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UI/ToastMessage.h"

#include "ToastStack.generated.h"

class APlayerController;
class UToastStackWidget;
class UToastWidget;
class UWildforgeUIManager;

/**
 * 多提示堆叠器：每个本地玩家一份，由 `UWildforgeUIManager` 懒创建并持有。
 *
 * ## 职责
 *
 * 把「一次显示一条、排队播放」升级成「同时显示最多 N 条、上下堆叠」：
 *
 *   - 每条提示一个独立的 `UToastWidget` 实例（各自计时、各自播放 WBP 动画）；
 *   - 从上到下按到达顺序排列（第 0 格最旧，最后一行最新）；
 *   - 某一格播完后，它下面的提示整体上移一格，**各自的剩余时间不变**（每条自己计时，
 *     这里只改控件的纵向偏移，不碰任何计时器）；
 *   - 待显示队列里还有提示时，取队头补进空出来的最后一格。
 *
 * 每条提示都用**新建**的控件实例，播完即移除：WBP 淡出动画会把个别子控件的
 * 不透明度写到 0（当前皮肤：淡入只动 BackgroundBorder、淡出连 MessageText 一起动），
 * 复用同一个控件会让下一条提示只看见背景、看不见文本（见 bug-149）。
 *
 * ## 布局
 *
 * 每个提示控件不再直接挂到视口，而是加进一个纯 C++ 构建的 `UToastStackWidget`
 *（铺满视口的根 CanvasPanel + 顶部居中的 `VerticalBox`）：
 *
 *   - 纵向排列完全交给 `VerticalBox`，任意多条都**不会重叠**；
 *   - 移除某一格后 UMG 自动重排，下面的提示整体上移一格；
 *   - 每条提示的计时在各自控件里，重排**不影响剩余时间**；
 *   - 行间距写进 VerticalBoxSlot 的 padding（提示 WBP 类默认值上的 `RowGap`），
 *     顶部留白同样来自类默认值上的 `TopPadding`。
 *
 * 卡片高度不需要在这里测量：VerticalBox 直接使用每个提示控件的期望尺寸。
 *
 * ## 队列与合并
 *
 * 默认**不合并**相同文本：每次投递都新开一格（重复触发会一直占到 `MaxVisibleToasts`
 * 满，再进入队列），便于确认同时显示数量。
 * 如果希望「相同文本只保留一条」，在提示 WBP 类默认值里打开 `bMergeSameText`：
 * 此时正在显示的相同文本只重置停留计时，队列尾部的相同文本直接替换。
 * 队列上限沿用 WBP 类默认值上的 `MaxQueuedMessages`。
 *
 * 纯客户端对象：不复制、没有 RPC；由管理器在本地玩家销毁 / 换提示皮肤时清空。
 */
UCLASS()
class WILDFORGE_API UToastStack : public UObject {
  GENERATED_BODY()

public:
  /** 创建堆叠器（由 `UWildforgeUIManager` 调用，Outer 为管理器本身）。 */
  static UToastStack *Create(UWildforgeUIManager *Owner,
                             TSubclassOf<UToastWidget> InEntryClass,
                             int32 InZOrder);

  /**
   * 投递一条提示。
   *
   * @return 真正开始显示这条提示的控件；暂时没格子、进了待显示队列时返回 nullptr。
   */
  UToastWidget *ShowToastMessage(const FToastMessage &Message);

  /**
   * 清空提示。
   *
   * @param bClearActive true = 正在显示的各条也立即收起；false = 只丢队列，
   *                     让正在显示的自然播完（播完后不再补位）。
   */
  void Clear(bool bClearActive);

  /** 清空队列并移除所有已创建的提示控件（本地玩家销毁 / 换提示皮肤时调用）。 */
  void Shutdown();

  /** 是否还有正在显示的提示。 */
  bool IsActive() const { return ActiveEntries.Num() > 0; }

  /** 正在显示的提示数量。 */
  int32 GetActiveCount() const { return ActiveEntries.Num(); }

  /** 待显示队列中的提示数量。 */
  int32 GetPendingCount() const { return Backlog.Num(); }

  /** 最上面（最早开始）的那条提示控件；没有时返回 nullptr。 */
  UToastWidget *GetTopEntry() const;

  /** 这个堆叠器使用的提示控件类。 */
  TSubclassOf<UToastWidget> GetEntryClass() const { return EntryClass; }

private:
  /** 创建一条提示控件并立刻开始显示；失败返回 nullptr。 */
  UToastWidget *SpawnEntry(const FToastMessage &Message);

  /** 控件播完回调：移除该格、其余上移、从队列补位。 */
  void HandleToastFinished(UToastWidget *Sender, const FToastMessage &Toast);

  /** 解开委托并把控件从 VerticalBox 移除（之后由 GC 回收）。 */
  void RemoveEntry(UToastWidget *Entry);

  /** 把队列里的提示补进空出来的格子（直到堆满或队列空）。 */
  void FillFreeSlots();

  /** 从控件类默认值读「同时显示上限」（<= 0 按 1 处理）。 */
  int32 ResolveMaxVisibleToasts();

  /** 从控件类默认值读堆叠容器距屏幕顶边的距离（像素）。 */
  float ResolveTopPadding() const;

  /** 从控件类默认值读「待显示队列上限」（<= 0 表示不限制）。 */
  int32 ResolveMaxQueuedMessages() const;

  /** 从控件类默认值读「相同文本是否合并」（默认 false = 每次新开一格）。 */
  bool ResolveMergeSameText() const;

  /** 提示控件类的类默认值对象（读 MaxVisibleToasts / RowGap / MaxQueuedMessages）。 */
  const UToastWidget *GetEntryDefaults() const;

  /** 本机 PlayerController（创建控件用）。 */
  APlayerController *GetOwningPlayerController() const;

  /** 创建堆叠器的管理器（弱引用：管理器销毁后本对象不该续命）。 */
  TWeakObjectPtr<UWildforgeUIManager> OwnerManager;

  /** 每个提示控件实例使用的 WBP 类。 */
  UPROPERTY(Transient)
  TSubclassOf<UToastWidget> EntryClass;

  /** 堆叠容器（纯 C++ 构建，挂到视口一次；提示控件都加进它的 VerticalBox）。 */
  UPROPERTY(Transient)
  TObjectPtr<UToastStackWidget> HostWidget;

  /** 正在显示的提示控件，按显示顺序（第 0 个在最上面）。 */
  UPROPERTY(Transient)
  TArray<TObjectPtr<UToastWidget>> ActiveEntries;

  /** 待显示队列（按到达顺序）。 */
  UPROPERTY(Transient)
  TArray<FToastMessage> Backlog;

  /** 提示控件挂到视口的层级。 */
  int32 ZOrder = 0;

  /** 「同时显示上限」配置非法时的告警是否已经打过。 */
  bool bWarnedAboutMaxVisible = false;
};
