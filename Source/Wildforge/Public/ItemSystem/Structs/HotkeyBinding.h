// HotkeyBinding.h
#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/InputChord.h"
#include "InputTriggers.h"
#include "HotkeyBinding.generated.h"

USTRUCT(BlueprintType)
struct FHotkeyBinding
{
    GENERATED_BODY()

    // 快捷键组合（按键+修饰键）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotkey")
    FInputChord Chord;

    // 要绑定的函数名（在拥有此组件的Actor蓝图中实现）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotkey")
    FName FunctionName;

    // 触发事件类型（Started / Triggered / Completed 等）
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hotkey")
    ETriggerEvent TriggerEvent = ETriggerEvent::Started;
};