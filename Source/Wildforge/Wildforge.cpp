// Fill out your copyright notice in the Description page of Project Settings.

#include "Wildforge.h"

#include "Utils/WildforgeLog.h"
#include "Modules/ModuleManager.h"

/** 游戏模块：在启动/关闭时挂载与卸载日志的异步文件输出设备。 */
class FWildforgeModule : public FDefaultGameModuleImpl {
public:
  virtual void StartupModule() override {
    FDefaultGameModuleImpl::StartupModule();
    WildforgeLogging::RegisterFileOutput();
  }

  virtual void ShutdownModule() override {
    WildforgeLogging::UnregisterFileOutput();
    FDefaultGameModuleImpl::ShutdownModule();
  }
};

IMPLEMENT_PRIMARY_GAME_MODULE(FWildforgeModule, Wildforge, "Wildforge");
