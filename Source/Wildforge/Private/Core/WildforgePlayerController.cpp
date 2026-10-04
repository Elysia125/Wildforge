// Fill out your copyright notice in the Description page of Project Settings.

#include "Core/WildforgePlayerController.h"

void AWildforgePlayerController::ShowInventory() {
  if (!MainUserWidget) {
    return;
  }

  MainUserWidget->ShowInventory();

  if (MainUserWidget->bIsInventoryOpen) {
    // 打开背包：显示鼠标、允许点击 UI，同时保留游戏输入
    FInputModeGameAndUI InputMode;
    InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    InputMode.SetHideCursorDuringCapture(false);
    SetInputMode(InputMode);
    bShowMouseCursor = true;
  } else {
    // 关闭背包：隐藏鼠标、恢复纯游戏输入
    SetInputMode(FInputModeGameOnly());
    bShowMouseCursor = false;
  }
}