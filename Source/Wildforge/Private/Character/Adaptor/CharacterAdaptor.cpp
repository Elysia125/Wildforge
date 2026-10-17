// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Adaptor/CharacterAdaptor.h"

#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"

FString UCharacterAdaptor::GetDebugString() const {
  // 空资源是**合法配置**（见头文件：网格/动画蓝图留空 = 保留蓝图默认值，
  // 蒙太奇留空 = 明确清空），所以这里统一显示「未配置」而不是当成错误。
  auto ResourceName = [](const UObject *Object) -> FString {
    return Object != nullptr ? Object->GetName() : FString(TEXT("未配置"));
  };

  const FString MeshTransform =
      bOverrideMeshTransform ? MeshRelativeTransform.ToHumanReadableString()
                             : FString(TEXT("不覆盖"));

  return FString::Printf(
      TEXT("网格=%s，动画蓝图=%s，攻击=%d 段，")
      TEXT("翻滚=%s[%s]，滑行=%s[%s]，加速=%s，闪现=%s，")
      TEXT("趴下(站立)=%s[%s]，趴下(奔跑)=%s[%s]，起身=%s[%s]，网格变换=%s"),
      *ResourceName(SkeletalMesh.Get()),
      AnimClass.Get() != nullptr ? *AnimClass->GetName() : TEXT("未配置"),
      AttackMontageList.Num(), *ResourceName(LandRollMontage.Get()),
      *LandRollSectionName.ToString(), *ResourceName(SlideMontage.Get()),
      *SlideSectionName.ToString(), *ResourceName(SprintMontage.Get()),
      *ResourceName(BlinkMontage.Get()), *ResourceName(ProneFromStandMontage.Get()),
      *ProneFromStandSectionName.ToString(),
      *ResourceName(ProneFromRunMontage.Get()),
      *ProneFromRunSectionName.ToString(),
      *ResourceName(ProneToStandMontage.Get()), *ProneToStandSectionName.ToString(),
      *MeshTransform);
}
