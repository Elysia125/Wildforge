#pragma once
#include "AttackMontageData.generated.h"

USTRUCT(BlueprintType)
struct FAttackMontageData {
  GENERATED_BODY()

  // 蒙太奇动画
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  UAnimMontage *Montage;

  // 起始的 Section 名字，默认为 "Default"
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attack")
  TArray<FName> SectionNames = { NAME_None };
};