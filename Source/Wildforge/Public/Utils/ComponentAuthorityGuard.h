// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"

/**
 * UActorComponent 子类的「开发期权威门禁」宏。
 *
 * 为什么需要它：本项目的权威函数都带 `BlueprintAuthorityOnly`，而它**不具备任何
 * 自动 RPC 转发能力**——它只做「非权威端拒绝执行」（callspace = `Absorbed`，
 * `Actor.cpp:5432`）。结果是客户端调用被引擎**静默丢弃**：单机 / 监听服务器上
 * 看起来一切正常，联机客户端上直接失效，这类 bug 极难排查。
 *
 * 本宏把「静默丢弃」变成「立刻可见的错误日志 + 安全返回」，让误用第一时间暴露。
 *
 * 用法（调用点必须是权威函数体的第一行）：
 *
 *     bool UMyComponent::DoThing_Implementation() {
 *       WF_COMPONENT_AUTHORITY_GUARD(false);   // 返回 bool 的门禁
 *       // ...
 *     }
 *
 *     void UMyComponent::Reset_Implementation() {
 *       WF_COMPONENT_AUTHORITY_GUARD(void());  // 返回 void 的门禁
 *       // ...
 *     }
 *
 * ⚠️ 组件里**不能**直接写 `HasAuthority()`：那是 `AActor` 的方法，
 * `UActorComponent` 上根本没有该标识符，写了会编译失败
 * （`error C3861: "HasAuthority": 找不到标识符`）。
 * 本宏内部用的是 `IsAuthoritativeForActorComponent(this)`（即
 * `GetOwner()->HasAuthority()`，含空指针保护），见 `Utils/WildforgeAuthority.h`。
 *
 * `ReturnValue` 是**返回值表达式**而不是类型：返回引用就传 `FVector()`，
 * 返回 void 就传 `void()`（`return void();` 在 C++ 里是合法的）。
 */
#define WF_COMPONENT_AUTHORITY_GUARD(ReturnValue)                              \
  do {                                                                         \
    if (!IsAuthoritativeForActorComponent(this)) {                             \
      WFLOG_ERROR(                                                             \
          "%s 在非权威端被调用，已忽略；客户端请改用对应的 Server_* RPC。",   \
          *FString(__FUNCTION__));                                             \
      return ReturnValue;                                                      \
    }                                                                          \
  } while (false)
