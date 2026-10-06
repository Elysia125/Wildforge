// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Blueprint/DragDropOperation.h"
#include "Blueprint/UserWidget.h" // SourceWidget 的弱引用需要完整类型
#include "ItemSystem/Components/ItemContainer.h"
#include "ItemSystem/Enums/ContainerType.h"
#include "ItemSystem/Structs/ItemInfo.h"

#include "InventorySlotDragDropOperation.generated.h"

/**
 *
 */
UCLASS()
class WILDFORGE_API UInventorySlotDragDropOperation
    : public UDragDropOperation {
  GENERATED_BODY()
public:
  UPROPERTY(BlueprintReadWrite, Category = "Drag")
  int32 SourceSlotIndex = INDEX_NONE;
  
  UPROPERTY(BlueprintReadWrite, Category = "Drag")
  EContainerType SourceContainerType = EContainerType::PlayerStorage;

  UPROPERTY(BlueprintReadWrite, Category = "Drag")
  FItemInformation ItemInfo;

  /** 记录源控件，便于拖拽取消时清理 */
  UPROPERTY(BlueprintReadWrite, Category = "Drag")
  UItemContainer *SourceContainer = nullptr;

  /**
   * 发起拖拽的源控件。
   * 用弱引用而非裸指针：拖拽期间若容器广播变更导致网格刷新、源控件被
   * RemoveFromParent/销毁，裸指针会悬空；弱引用会自动失效，配合
   * SetRenderOpacity 自身对已销毁控件的空操作语义，不会崩，也不会误操作新控件。
   * 恢复显示仍以源控件自身（Drop/Cancelled/NativeDestruct）为主，这里只作兜底。
   */
  UPROPERTY(BlueprintReadWrite, Category = "Drag")
  TWeakObjectPtr<UUserWidget> SourceWidget;

  /** 兜底恢复源控件显示（拖拽视觉反馈复位），控件已失效时安全跳过 */
  void RestoreSourceWidget() const;
};
