// HotkeyUtils.h
#pragma once

#include "CoreMinimal.h"

#include "Framework/Commands/InputChord.h"

/**
 * 把 FInputChord 转换成可读字符串，例如 "Ctrl+A"、"Shift+Alt+F1"
 * 修饰键顺序固定为 Cmd+Ctrl+Alt+Shift（与常见习惯一致，可按需调整）
 */
inline FString InputChordToString(const FInputChord &Chord) {
  if (!Chord.Key.IsValid()) {
    return TEXT("<None>");
  }

  TArray<FString> Parts;

  // 修饰键按固定顺序加入，避免每次顺序不同
  if (Chord.bCmd) {
    Parts.Add(TEXT("Cmd"));
  }
  if (Chord.bCtrl) {
    Parts.Add(TEXT("Ctrl"));
  }
  if (Chord.bAlt) {
    Parts.Add(TEXT("Alt"));
  }
  if (Chord.bShift) {
    Parts.Add(TEXT("Shift"));
  }

  // 主键名
  Parts.Add(Chord.Key.ToString());

  return FString::Join(Parts, TEXT("+"));
}