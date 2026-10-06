// Fill out your copyright notice in the Description page of Project Settings.

#include "ItemSystem/UI/InventorySlotDragDropOperation.h"

#include "Blueprint/UserWidget.h"

void UInventorySlotDragDropOperation::RestoreSourceWidget() const {
  // SourceWidget 是弱引用：源控件已被销毁时 Pin() 返回 nullptr，直接跳过。
  // 即便 UObject 还活着但 Slate 控件已被释放，UWidget::SetRenderOpacity 内部
  // 走 GetCachedWidget()，拿不到 Slate 控件时是空操作，所以这里没有悬空风险。
  if (UUserWidget *Widget = SourceWidget.Get()) {
    Widget->SetRenderOpacity(1.f);
  }
}
