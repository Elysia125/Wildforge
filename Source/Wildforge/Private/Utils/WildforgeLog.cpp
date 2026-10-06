// Fill out your copyright notice in the Description page of Project Settings.

#include "Utils/WildforgeLog.h"

#include "Containers/StringConv.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceFile.h"       // FAsyncWriter
#include "Misc/OutputDeviceRedirector.h" // GLog::AddOutputDevice
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

DEFINE_LOG_CATEGORY(LogWildforge);

namespace {

constexpr int32 GNumLevels = 3;

const TCHAR *LevelName(int32 Level) {
  switch (Level) {
  case 1:
    return TEXT("Warning");
  case 2:
    return TEXT("Error");
  default:
    return TEXT("Info");
  }
}

int32 LevelIndex(ELogVerbosity::Type Verbosity) {
  switch (Verbosity) {
  case ELogVerbosity::Error:
  case ELogVerbosity::Fatal:
    return 2;
  case ELogVerbosity::Warning:
    return 1;
  default:
    return 0;
  }
}

FString DateStamp() { return FDateTime::Now().ToString(TEXT("%Y-%m-%d")); }

/**
 * 单个等级的文件写入器：独立的 FAsyncWriter（后台线程 + 环形缓冲）与独立临界区。
 * 调用线程只把 UTF-8 字节拷进缓冲即返回，磁盘 I/O 全在后台线程完成。
 */
class FLevelFileWriter {
public:
  explicit FLevelFileWriter(int32 InLevel) : Level(InLevel) {}

  void Write(const TCHAR *Text) {
    FScopeLock Lock(&CriticalSection);
    EnsureOpen();
    if (!Writer) {
      return;
    }
    FTCHARToUTF8 Utf8(Text);
    Writer->Serialize((uint8 *)Utf8.Get(), (int64)Utf8.Length());
  }

  void Flush() {
    FScopeLock Lock(&CriticalSection);
    if (Writer) {
      Writer->Flush();
    }
  }

  void Close() {
    FScopeLock Lock(&CriticalSection);
    CloseInternal();
  }

private:
  void EnsureOpen() {
    const FString Today = DateStamp();
    if (Today == CurrentDate) {
      return; // 已打开；失败也记下日期，避免每行重试
    }
    CloseInternal();
    CurrentDate = Today;

    const FString Directory =
        FPaths::ProjectSavedDir() / TEXT("Logs/Wildforge") / Today;
    IFileManager::Get().MakeDirectory(*Directory, /*Tree=*/true);

    const FString Filename =
        Directory / FString(LevelName(Level)) + TEXT(".log");
    Archive = IFileManager::Get().CreateFileWriter(
        *Filename, FILEWRITE_Silent | FILEWRITE_Append);
    if (Archive) {
      Writer = new FAsyncWriter(*Archive,
                                FAsyncWriter::EThreadNameOption::Sequential);
    }
  }

  void CloseInternal() {
    if (Writer) {
      Writer->Flush();
      delete Writer;
      Writer = nullptr;
    }
    if (Archive) {
      delete Archive; // FArchive 析构负责关闭文件
      Archive = nullptr;
    }
    CurrentDate.Reset();
  }

  int32 Level;
  FCriticalSection CriticalSection;
  FString CurrentDate;
  FArchive *Archive = nullptr;
  FAsyncWriter *Writer = nullptr;
};

/**
 * 只处理 LogWildforge 类别的 FOutputDevice：按冗余度分派到三个等级写入器。
 * 挂到 GLog 后，任何 UE_LOG(LogWildforge, ...) 都会落盘，无需在调用点做任何处理。
 */
class FWildforgeLogDevice : public FOutputDevice {
public:
  virtual void Serialize(const TCHAR *V, ELogVerbosity::Type Verbosity,
                         const FName &Category) override {
    // 只处理本类别；类别名即 DECLARE_LOG_CATEGORY_EXTERN 的第一个参数
    static const FName WildforgeCategoryName(TEXT("LogWildforge"));
    if (Category != WildforgeCategoryName) {
      return;
    }
    const int32 Index = LevelIndex(Verbosity);
    const FString Line = FString::Printf(
        TEXT("%s [%s] %s%s"),
        *FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S.%s")),
        LevelName(Index), V, LINE_TERMINATOR);
    Writers[Index].Write(*Line);
  }

  virtual void Flush() override {
    for (FLevelFileWriter &W : Writers) {
      W.Flush();
    }
  }

  virtual void TearDown() override {
    for (FLevelFileWriter &W : Writers) {
      W.Close();
    }
  }

  // 日志可从任意线程/崩溃线程产生；设备内部已是线程安全的
  virtual bool CanBeUsedOnAnyThread() const override { return true; }
  virtual bool CanBeUsedOnPanicThread() const override { return true; }

private:
  FLevelFileWriter Writers[GNumLevels] = {FLevelFileWriter(0),
                                          FLevelFileWriter(1),
                                          FLevelFileWriter(2)};
};

FWildforgeLogDevice *GFileDevice = nullptr;

} // namespace

void WildforgeLogging::RegisterFileOutput() {
  if (GFileDevice || !GLog) {
    return;
  }
  GFileDevice = new FWildforgeLogDevice();
  GLog->AddOutputDevice(GFileDevice);
}

void WildforgeLogging::UnregisterFileOutput() {
  if (!GFileDevice) {
    return;
  }
  if (GLog) {
    GLog->RemoveOutputDevice(GFileDevice);
  }
  GFileDevice->TearDown();
  delete GFileDevice;
  GFileDevice = nullptr;
}

void WildforgeLogging::FlushFileOutput() {
  if (GFileDevice) {
    GFileDevice->Flush();
  }
}

FString UWildforgeLog::GetLogDirectory() {
  return FPaths::ProjectSavedDir() / TEXT("Logs/Wildforge") / DateStamp();
}

void UWildforgeLog::Log(EWildforgeLogLevel Level, const FString &Message,
                        const FString &Context) {
  const FString Body =
      Context.IsEmpty() ? Message
                        : FString::Printf(TEXT("[%s] %s"), *Context, *Message);

  // 输出到终端 / Output Log；文件由已注册的 FWildforgeLogDevice 异步落盘。
  switch (Level) {
  case EWildforgeLogLevel::Warning:
    UE_LOG(LogWildforge, Warning, TEXT("%s"), *Body);
    break;
  case EWildforgeLogLevel::Error:
    UE_LOG(LogWildforge, Error, TEXT("%s"), *Body);
    break;
  default:
    UE_LOG(LogWildforge, Log, TEXT("%s"), *Body);
    break;
  }
}

void UWildforgeLog::Info(const FString &Message, const FString &Context) {
  Log(EWildforgeLogLevel::Info, Message, Context);
}

void UWildforgeLog::Warning(const FString &Message, const FString &Context) {
  Log(EWildforgeLogLevel::Warning, Message, Context);
}

void UWildforgeLog::Error(const FString &Message, const FString &Context) {
  Log(EWildforgeLogLevel::Error, Message, Context);
}
