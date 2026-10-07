// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/AttackComponent.h"

#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"

// Sets default values for this component's properties
UAttackComponent::UAttackComponent() {
  // Set this component to be initialized when the game starts, and to be ticked
  // every frame.  You can turn these features off to improve performance if you
  // don't need them.
  PrimaryComponentTick.bCanEverTick = true;

  // ...
}

// Called when the game starts
void UAttackComponent::BeginPlay() {
  Super::BeginPlay();

  // ...
}

// Called every frame
void UAttackComponent::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // ...
}

bool UAttackComponent::Attack_Implementation() {
  if (!bCanAttack) {
    return false;
  }
  if (!bIsAttacking) {
    bIsAttacking = true;
    LastAttackTime = GetWorld()->GetTimeSeconds();
  } else {
    if (!bCanCombo) {
      return false;
    }
    if (GetWorld()->GetTimeSeconds() - LastAttackTime < AttackCooldown) {
      return false;
    }
    bCanCombo = false;
  }

  PlayAttackMontage();
  MontageSectionIndex++;
  if (MontageSectionIndex >=
      AttackMontageList[AttackMontageIndex].SectionNames.Num()) {
    MontageSectionIndex = 0;
    AttackMontageIndex++;
    if (AttackMontageIndex >= AttackMontageList.Num()) {
      AttackMontageIndex = 0;
    }
  }
  return true;
}

void UAttackComponent::PlayAttackMontage_Implementation() {
  if (!AttackMontageList.IsValidIndex(AttackMontageIndex)) {
    AttackMontageIndex = 0;
  }
  FAttackMontageData &Data = AttackMontageList[AttackMontageIndex];
  if (!Data.SectionNames.IsValidIndex(MontageSectionIndex)) {
    AttackMontageIndex++;
    MontageSectionIndex = 0;
    PlayAttackMontage();
    return;
  }

  if (Data.Montage && GetOwner()) {
    if (ACharacter *OwnerChar = Cast<ACharacter>(GetOwner())) {
      if (UAnimInstance *AnimInst = OwnerChar->GetMesh()->GetAnimInstance()) {
        // 1. 先解绑，防止重复绑定（很重要！）
        AnimInst->OnMontageEnded.RemoveDynamic(
            this, &UAttackComponent::OnAttackMontageEnded);

        // 2. 重新绑定
        AnimInst->OnMontageEnded.AddDynamic(
            this, &UAttackComponent::OnAttackMontageEnded);
        // 播放蒙太奇，并指定从哪个 Section 开始
        AnimInst->Montage_Play(Data.Montage);
        if (!Data.SectionNames[MontageSectionIndex].IsNone()) {
          AnimInst->Montage_JumpToSection(
              Data.SectionNames[MontageSectionIndex], Data.Montage);
        }
      }
    }
  }
}

// 回调实现
void UAttackComponent::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
    // 判断是不是我们正在播放的攻击蒙太奇（防止误触发其他蒙太奇）
	if(!AttackMontageList.IsValidIndex(AttackMontageIndex)) return;
	FAttackMontageData &Data = AttackMontageList[AttackMontageIndex];
	if (!Data.Montage) return;
    if (Montage != Data.Montage) return;

    ResetAttackState();
    
    // 如果需要，通知蓝图（比如恢复移动、播放待机等）
    OnAttackFinished.Broadcast();
}
void UAttackComponent::ResetAttackState_Implementation() {
  bIsAttacking = false;
  MontageSectionIndex = 0;
  AttackMontageIndex = 0;
  bCanCombo = false;
  LastAttackTime = 0.0f;
}