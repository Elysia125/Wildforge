// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"

/**
 * 权威端判断（供 UActorComponent 子类使用）。
 *
 * 为什么需要它：`HasAuthority()` 是 **AActor** 的方法，`UActorComponent` 上根本没有
 * 这个标识符（组件要经 GetOwner() 转发）。在组件里直接写 `HasAuthority()` 会编译失败：
 *
 *     error C3861: "HasAuthority": 找不到标识符
 *
 * 本函数把这一步包起来，可安全用于组件头文件里的内联函数体。
 * 语义与 `AActor::HasAuthority()` 完全一致：本端是权威端（Standalone / 专用服务器 /
 * 监听服务器主机）时返回 true，远程客户端返回 false。
 */
inline bool IsAuthoritativeForActorComponent(
    const UActorComponent *Component) {
  const AActor *Owner = Component ? Component->GetOwner() : nullptr;
  return Owner != nullptr && Owner->HasAuthority();
}
