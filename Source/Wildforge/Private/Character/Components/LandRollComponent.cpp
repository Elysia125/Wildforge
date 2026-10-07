// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Components/LandRollComponent.h"

#include "Utils/AuthorityGuard.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Utils/WildforgeAuthority.h"
// Sets default values for this component's properties
ULandRollComponent::ULandRollComponent() {
  // Set this component to be initialized when the game starts, and to be ticked
  // every frame.  You can turn these features off to improve performance if you
  // don't need them.
  PrimaryComponentTick.bCanEverTick = true;

  // ...
}

// Called when the game starts
void ULandRollComponent::BeginPlay() {
  Super::BeginPlay();

  // ...
}

// Called every frame
void ULandRollComponent::TickComponent(
    float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction *ThisTickFunction) {
  Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

  // ...
}

void ULandRollComponent::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const {
  Super::GetLifetimeReplicatedProps(OutLifetimeProps);

  // 这几个都是「服务器写、客户端只读」的状态。
  // COND_SimulatedOnly：只发给模拟端 ——
  // 服务器自己不需要收，自主代理（本地玩家）
  // 也不需要（它的动作本来就是本地输入驱动的），这样能省掉一份无用带宽。
  DOREPLIFETIME_CONDITION(ULandRollComponent, bIsRolling, COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, LastLandRollTime,
                          COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, LandRollCooldown,
                          COND_SimulatedOnly);
  DOREPLIFETIME_CONDITION(ULandRollComponent, MontagePlayRate,
                          COND_SimulatedOnly);
}

bool ULandRollComponent::LandRoll_Implementation() {
  WF_AUTHORITY_GUARD(false);
  if (bIsRolling) {
    return false;
  }
  if (GetWorld()->GetTimeSeconds() - LastLandRollTime < LandRollCooldown) {
    return false;
  }
  bIsRolling = true;
  LastLandRollTime = GetWorld()->GetTimeSeconds();
  OnLandRollStarted.Broadcast();
  Multicast_PlayLandRollMontage();
  return true;
}

bool ULandRollComponent::Server_LandRoll_Validate() { return true; }

void ULandRollComponent::Server_LandRoll_Implementation() { LandRoll(); }

void ULandRollComponent::Multicast_PlayLandRollMontage_Implementation() {
  const bool bAuthority = IsAuthoritativeForActorComponent(this);
  if (bAuthority) {
    PlayLandRollMontage();
  } else {
    PlayLandRollMontageInternal();
  }
}

bool ULandRollComponent::PlayLandRollMontage_Implementation() {
  WF_AUTHORITY_GUARD(false);
  return PlayLandRollMontageInternal();
}
void ULandRollComponent::OnLandRollMontageFinished(UAnimMontage *Montage,
                                                   bool bInterrupted) {
  bIsRolling = false;
  OnLandRollFinished.Broadcast();
}

bool ULandRollComponent::PlayLandRollMontageInternal() {
  ACharacter *OwnerChar = Cast<ACharacter>(GetOwner());
  if (OwnerChar == nullptr) {
    return false;
  }

  USkeletalMeshComponent *Mesh = OwnerChar->GetMesh();
  UAnimInstance *AnimInst = Mesh ? Mesh->GetAnimInstance() : nullptr;
  if (AnimInst == nullptr) {
    return false;
  }

  // 先解绑再绑定，避免同一回调被重复注册（每次攻击都会走这里）
  AnimInst->OnMontageEnded.RemoveDynamic(
      this, &ULandRollComponent::OnLandRollMontageFinished);
  AnimInst->OnMontageEnded.AddDynamic(
      this, &ULandRollComponent::OnLandRollMontageFinished);

  // 交叉淡入：连击立刻切段时用它做衔接（0 = 硬切，起手时就是这样）
  const float PlayLength =
      AnimInst->Montage_Play(LandRollMontage, MontagePlayRate);
  if (PlayLength <= 0.0f) {
    return false;
  }
  return true;
}