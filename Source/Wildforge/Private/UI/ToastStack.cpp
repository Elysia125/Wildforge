#include "UI/ToastStack.h"

#include "Blueprint/UserWidget.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "GameFramework/PlayerController.h"
#include "UI/ToastStackWidget.h"
#include "UI/ToastWidget.h"
#include "UI/WildforgeUIManager.h"
#include "Utils/WildforgeLog.h"

UToastStack *UToastStack::Create(UWildforgeUIManager *Owner,
                                 TSubclassOf<UToastWidget> InEntryClass,
                                 int32 InZOrder) {
  if (!Owner) {
    WFLOG_ERROR("[ToastStack] Create 收到空的管理器，无法创建提示堆叠器");
    return nullptr;
  }
  if (!InEntryClass) {
    WFLOG_ERROR("[ToastStack] Create 收到空的提示控件类，无法创建提示堆叠器");
    return nullptr;
  }

  UToastStack *Stack = NewObject<UToastStack>(Owner);
  Stack->OwnerManager = Owner;
  Stack->EntryClass = InEntryClass;
  Stack->ZOrder = InZOrder;

  APlayerController *PlayerController = Stack->GetOwningPlayerController();
  if (!PlayerController) {
    WFLOG_ERROR("[ToastStack] Create: 拿不到本地 PlayerController，"
                "无法创建提示堆叠容器（专用服务器没有本地 UI）");
    return nullptr;
  }

  // 堆叠容器只创建一次并常驻视口：提示控件作为它的 VerticalBox 子项进出，
  // 纵向排列 / 上移全部由 UMG 的布局系统完成。
  UToastStackWidget *HostWidget = CreateWidget<UToastStackWidget>(
      PlayerController, UToastStackWidget::StaticClass());
  if (!HostWidget) {
    WFLOG_ERROR("[ToastStack] Create: 创建提示堆叠容器失败"
                "（UToastStackWidget::StaticClass()）");
    return nullptr;
  }

  HostWidget->ConfigureLayout(Stack->ResolveTopPadding());
  HostWidget->AddToViewport(InZOrder);
  Stack->HostWidget = HostWidget;

  WFLOG_INFO("[ToastStack] 堆叠器已创建（提示控件类 %s，ZOrder=%d，最多同时 %d 条，"
             "待显示上限 %d 条，相同文本合并=%s；VerticalBox 负责纵向排列，不会重叠）",
             *InEntryClass->GetName(), InZOrder,
             Stack->ResolveMaxVisibleToasts(), Stack->ResolveMaxQueuedMessages(),
             Stack->ResolveMergeSameText() ? TEXT("是") : TEXT("否"));
  return Stack;
}

// ================= 对外入口 =================

UToastWidget *UToastStack::ShowToastMessage(const FToastMessage &Message) {
  if (Message.Message.IsEmpty()) {
    WFLOG_WARNING("[ToastStack] 收到空文本提示，已忽略");
    return nullptr;
  }

  // 1/2) 相同文本合并（默认关闭）：打开后，正在显示的相同文本只重置停留计时，
  //      队列尾部的相同文本直接替换最新样式。默认关闭时重复触发会占用多格，
  //      这是用户明确要求的行为（见 bug-166）。
  if (ResolveMergeSameText()) {
    // 1) 相同文本正在显示（淡出中的那条不算：它马上就会消失，让它正常收尾）：
    //    交给那一格自己处理——控件内部的「同文本合并」会重置它的停留计时。
    for (int32 Index = 0; Index < ActiveEntries.Num(); ++Index) {
      UToastWidget *Entry = ActiveEntries[Index];
      if (!Entry || Entry->IsToastFadingOut()) {
        continue;
      }
      if (Entry->GetActiveToast().Message.EqualTo(Message.Message)) {
        Entry->ShowToastMessage(Message);
        WFLOG_INFO("[ToastStack] 第 %d 格正在显示相同文本，只重置它的停留计时"
                   "（bMergeSameText=true，不新增格子）：%s",
                   Index, *Message.Message.ToString());
        return Entry;
      }
    }

    // 2) 队列尾部相同文本：替换成最新样式，避免同一句话排两次。
    if (Backlog.Num() > 0 && Backlog.Last().Message.EqualTo(Message.Message)) {
      Backlog.Last() = Message;
      WFLOG_INFO("[ToastStack] 队列尾部的相同提示已替换为最新样式"
                 "（bMergeSameText=true，待显示 %d 条）：%s",
                 Backlog.Num(), *Message.Message.ToString());
      return nullptr;
    }
  }

  // 3) 还有空位：立刻显示。
  const int32 MaxVisible = ResolveMaxVisibleToasts();
  if (ActiveEntries.Num() < MaxVisible) {
    return SpawnEntry(Message);
  }

  // 4) 已满：进队列。上限沿用提示 WBP 类默认值上的 MaxQueuedMessages（<= 0 不限）。
  const int32 MaxQueued = ResolveMaxQueuedMessages();
  if (MaxQueued > 0 && Backlog.Num() >= MaxQueued) {
    const FToastMessage DroppedToast = Backlog[0];
    Backlog.RemoveAt(0);
    WFLOG_WARNING("[ToastStack] 待显示队列已满（上限 %d 条），丢弃排队最久的一条：%s",
                  MaxQueued, *DroppedToast.Message.ToString());
  }

  Backlog.Add(Message);
  WFLOG_INFO("[ToastStack] %d 格已占满（上限 %d），新提示入队（待显示 %d 条）：%s",
             ActiveEntries.Num(), MaxVisible, Backlog.Num(),
             *Message.Message.ToString());
  return nullptr;
}

void UToastStack::Clear(bool bClearActive) {
  const int32 DroppedCount = Backlog.Num();
  Backlog.Empty();

  if (!bClearActive) {
    WFLOG_INFO("[ToastStack] 已清空待显示队列（%d 条），正在显示的 %d 条提示继续播完",
               DroppedCount, ActiveEntries.Num());
    return;
  }

  const int32 ActiveCount = ActiveEntries.Num();
  // 先把数组摘空再逐个清理：控件收尾时可能会触发 OnToastFinished（蓝图事件里又弹提示
  // 之类），此时它已经不在列表里，回调会直接忽略，不会二次移除 / 重排。
  TArray<TObjectPtr<UToastWidget>> EntriesToRemove = MoveTemp(ActiveEntries);
  ActiveEntries.Reset();

  for (UToastWidget *Entry : EntriesToRemove) {
    if (!Entry) {
      continue;
    }
    Entry->OnToastFinished.RemoveAll(this);
    Entry->ClearToasts(true); // 让 WBP 收尾（BP_OnToastHidden / BP_OnToastQueueEmptied）
    Entry->RemoveFromParent();
  }

  WFLOG_INFO("[ToastStack] 已清空提示：丢弃待显示 %d 条，移除正在显示 %d 条",
             DroppedCount, ActiveCount);
}

void UToastStack::Shutdown() {
  Clear(true);

  if (HostWidget) {
    WFLOG_INFO("[ToastStack] Shutdown: 移除提示堆叠容器（ZOrder=%d）", ZOrder);
    HostWidget->RemoveFromParent();
    HostWidget = nullptr;
  }
}

UToastWidget *UToastStack::GetTopEntry() const {
  return ActiveEntries.Num() > 0 ? ActiveEntries[0].Get() : nullptr;
}

// ================= 内部：创建 / 回收 / 布局 =================

UToastWidget *UToastStack::SpawnEntry(const FToastMessage &Message) {
  if (!EntryClass) {
    WFLOG_ERROR("[ToastStack] 提示控件类为空，无法创建提示控件");
    return nullptr;
  }

  if (!HostWidget) {
    WFLOG_ERROR("[ToastStack] 堆叠容器不存在（可能已被 Shutdown），"
                "无法显示提示：%s",
                *Message.Message.ToString());
    return nullptr;
  }

  UVerticalBox *Container = HostWidget->GetToastContainer();
  if (!Container) {
    WFLOG_ERROR("[ToastStack] 堆叠容器里没有 VerticalBox，无法显示提示：%s",
                *Message.Message.ToString());
    return nullptr;
  }

  APlayerController *PlayerController = GetOwningPlayerController();
  if (!PlayerController) {
    WFLOG_ERROR("[ToastStack] 拿不到本地 PlayerController，无法创建提示控件"
                "（类 %s；专用服务器没有本地 UI）",
                *EntryClass->GetName());
    return nullptr;
  }

  UToastWidget *Entry = CreateWidget<UToastWidget>(PlayerController, EntryClass);
  if (!Entry) {
    WFLOG_ERROR("[ToastStack] CreateWidget 失败（类 %s）。"
                "检查它是否是可实例化的 WBP、是否有 WidgetTree",
                *EntryClass->GetName());
    return nullptr;
  }

  Entry->OnToastFinished.AddUObject(this, &UToastStack::HandleToastFinished);

  // 加进 VerticalBox：纵向排列 / 后续上移全部由 UMG 的布局系统完成。
  // 行尾留 RowGap 像素；水平居中（卡片比容器窄时也不会贴左）。
  UVerticalBoxSlot *EntrySlot = Container->AddChildToVerticalBox(Entry);
  if (!EntrySlot) {
    WFLOG_ERROR("[ToastStack] 把提示控件加进 VerticalBox 失败（类 %s）",
                *EntryClass->GetName());
    Entry->OnToastFinished.RemoveAll(this);
    Entry->RemoveFromParent();
    return nullptr;
  }

  const float RowGap = FMath::Max(0.0f, Entry->GetRowGap());
  EntrySlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, RowGap));
  EntrySlot->SetHorizontalAlignment(HAlign_Center);
  EntrySlot->SetVerticalAlignment(VAlign_Top);

  ActiveEntries.Add(Entry);
  Entry->ShowToastMessage(Message);

  WFLOG_INFO("[ToastStack] 第 %d 格开始显示（已显示 %d/%d，待显示 %d 条，"
             "行间距 %.1f）：%s",
             ActiveEntries.Num() - 1, ActiveEntries.Num(),
             ResolveMaxVisibleToasts(), Backlog.Num(), RowGap,
             *Message.Message.ToString());
  return Entry;
}

void UToastStack::HandleToastFinished(UToastWidget *Sender,
                                      const FToastMessage &Toast) {
  if (!Sender) {
    return;
  }

  const int32 Index = ActiveEntries.IndexOfByKey(Sender);
  if (Index == INDEX_NONE) {
    // 已经被 Clear / Shutdown 移除（委托也已解绑），这里不需要再做任何事。
    return;
  }

  if (Sender->IsToastActive()) {
    // 控件内部队列又接了一条（管理器路径正常不会发生）：保留这一格，等它真正播完。
    WFLOG_INFO("[ToastStack] 第 %d 格的提示结束后又开始了新提示，保留该格：%s",
               Index, *Toast.Message.ToString());
    return;
  }

  WFLOG_INFO("[ToastStack] 第 %d 格播完，移除它；VerticalBox 会把其余 %d 格"
             "自动上移一格（各自剩余时间不变，待显示 %d 条）：%s",
             Index, ActiveEntries.Num() - 1, Backlog.Num(),
             *Toast.Message.ToString());

  ActiveEntries.RemoveAt(Index);
  RemoveEntry(Sender);

  // 移除控件后 UMG 自动重排；每条提示的计时在各自控件里，剩余时间不受影响。
  // 最后把队列里的下一条补进空出来的最后一格。
  FillFreeSlots();
}

void UToastStack::RemoveEntry(UToastWidget *Entry) {
  if (!Entry) {
    return;
  }
  Entry->OnToastFinished.RemoveAll(this);
  Entry->RemoveFromParent();
}

void UToastStack::FillFreeSlots() {
  const int32 MaxVisible = ResolveMaxVisibleToasts();
  while (Backlog.Num() > 0 && ActiveEntries.Num() < MaxVisible) {
    const FToastMessage NextToast = Backlog[0];
    Backlog.RemoveAt(0);

    WFLOG_INFO("[ToastStack] 从队列取出下一条补进第 %d 格（待显示 %d 条）：%s",
               ActiveEntries.Num(), Backlog.Num(),
               *NextToast.Message.ToString());

    if (!SpawnEntry(NextToast)) {
      // 创建失败（没有 PC / 类无效）：放回队头，避免这条提示悄悄消失。
      Backlog.Insert(NextToast, 0);
      WFLOG_ERROR("[ToastStack] 补位失败，这条提示已放回队列头部：%s",
                  *NextToast.Message.ToString());
      break;
    }
  }
}

float UToastStack::ResolveTopPadding() const {
  const UToastWidget *Defaults = GetEntryDefaults();
  return Defaults ? FMath::Max(0.0f, Defaults->GetTopPadding()) : 40.0f;
}

// ================= 内部：配置 =================

int32 UToastStack::ResolveMaxVisibleToasts() {
  const UToastWidget *Defaults = GetEntryDefaults();
  const int32 Configured = Defaults ? Defaults->GetMaxVisibleToasts() : 5;
  if (Configured < 1) {
    if (!bWarnedAboutMaxVisible) {
      bWarnedAboutMaxVisible = true;
      WFLOG_WARNING("[ToastStack] MaxVisibleToasts 配置为 %d（至少 1），按 1 处理",
                    Configured);
    }
    return 1;
  }
  return Configured;
}

int32 UToastStack::ResolveMaxQueuedMessages() const {
  const UToastWidget *Defaults = GetEntryDefaults();
  return Defaults ? Defaults->GetMaxQueuedMessages() : 5;
}

bool UToastStack::ResolveMergeSameText() const {
  const UToastWidget *Defaults = GetEntryDefaults();
  return Defaults ? Defaults->GetMergeSameText() : false;
}

const UToastWidget *UToastStack::GetEntryDefaults() const {
  if (EntryClass) {
    if (const UToastWidget *ClassDefaults =
            Cast<UToastWidget>(EntryClass->GetDefaultObject())) {
      return ClassDefaults;
    }
  }
  // 正常到不了：EntryClass 一定是可实例化的 WBP 子类。兜底避免空指针。
  return GetDefault<UToastWidget>();
}

APlayerController *UToastStack::GetOwningPlayerController() const {
  const UWildforgeUIManager *Manager = OwnerManager.Get();
  return Manager ? Manager->GetOwningPlayerController() : nullptr;
}
