// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Kismet/BlueprintFunctionLibrary.h"

#include "WildforgeLog.generated.h"

WILDFORGE_API DECLARE_LOG_CATEGORY_EXTERN(LogWildforge, Log, All);

/** 日志等级（内部使用；蓝图侧直接用 Info/Warning/Error 三个函数） */
enum class EWildforgeLogLevel : uint8 {
  Info,
  Warning,
  Error,
};

/**
 * 日志工具。
 *
 * 一次调用同时输出到两处：
 *   1) 终端 / Output Log —— 经 UE_LOG(LogWildforge, ...)（同步，引擎默认设备负责）
 *   2) 文件 —— Saved/Logs/Wildforge/<YYYY-MM-DD>/<Info|Warning|Error>.log
 *
 * 文件写入走引擎的 FAsyncWriter（环形缓冲 + 后台线程），调用线程只做一次内存拷贝，
 * 不做任何磁盘 I/O；且每个等级一个独立 Writer（独立锁 + 独立线程），等级之间零竞争。
 * 文件内容为 UTF-8（无 BOM），跨日期自动切换目录。
 *
 * C++ 侧可用 WFLOG_INFO / WFLOG_WARNING / WFLOG_ERROR 宏；蓝图可调同名函数。
 */
UCLASS()
class WILDFORGE_API UWildforgeLog : public UBlueprintFunctionLibrary {
  GENERATED_BODY()
public:
  /** 普通信息 */
  UFUNCTION(BlueprintCallable, Category = "Wildforge|Log")
  static void Info(const FString &Message, const FString &Context = TEXT(""));

  /** 警告 */
  UFUNCTION(BlueprintCallable, Category = "Wildforge|Log")
  static void Warning(const FString &Message,
                      const FString &Context = TEXT(""));

  /** 错误 */
  UFUNCTION(BlueprintCallable, Category = "Wildforge|Log")
  static void Error(const FString &Message, const FString &Context = TEXT(""));

  /** 通用入口（C++）：按等级输出到终端与文件 */
  static void Log(EWildforgeLogLevel Level, const FString &Message,
                  const FString &Context = FString());

  /** 当前日期的日志目录：Saved/Logs/Wildforge/<YYYY-MM-DD> */
  UFUNCTION(BlueprintPure, Category = "Wildforge|Log")
  static FString GetLogDirectory();
};

/**
 * 文件输出的注册接口。由 Wildforge 模块在 Startup/Shutdown 中调用一次，
 * 把异步文件设备挂到 GLog 上（此后任何 UE_LOG(LogWildforge, ...) 都会落盘）。
 */
namespace WildforgeLogging {
WILDFORGE_API void RegisterFileOutput();
WILDFORGE_API void UnregisterFileOutput();
/** 强制把缓冲区刷到磁盘（崩溃前、退出前可调用） */
WILDFORGE_API void FlushFileOutput();
} // namespace WildforgeLogging

// C++ 便捷宏（Format 需为字符串字面量）；是否在 Shipping 中保留由 LogWildforge
// 的编译期冗余度（DECLARE_LOG_CATEGORY_EXTERN 第三参）控制。
#define WFLOG_INFO(Format, ...)                                                \
  UWildforgeLog::Info(FString::Printf(TEXT(Format), ##__VA_ARGS__))
#define WFLOG_WARNING(Format, ...)                                             \
  UWildforgeLog::Warning(FString::Printf(TEXT(Format), ##__VA_ARGS__))
#define WFLOG_ERROR(Format, ...)                                               \
  UWildforgeLog::Error(FString::Printf(TEXT(Format), ##__VA_ARGS__))
