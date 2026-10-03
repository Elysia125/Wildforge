// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Components/CharacterAttributes.h"

#include "Net/UnrealNetwork.h"

// Sets default values for this component's properties
UCharacterAttributes::UCharacterAttributes()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// 组件参与复制（前提：宿主 Actor 的 bReplicates 也为 true）
	SetIsReplicatedByDefault(true);

	// ...
}

void UCharacterAttributes::GetLifetimeReplicatedProps(
    TArray<FLifetimeProperty> &OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 无条件下发（所有客户端可见），便于显示他人血条。
	// 若只想让本人看到自己的精确血量，改成 DOREPLIFETIME_CONDITION(..., COND_OwnerOnly)。
	DOREPLIFETIME(UCharacterAttributes, MaxHealth);
	DOREPLIFETIME(UCharacterAttributes, Health);
}

void UCharacterAttributes::NotifyHealthChanged()
{
	OnHealthChanged.Broadcast(Health);
}

void UCharacterAttributes::OnRep_Health()
{
	// 客户端收到服务器同步过来的血量，通知 UI
	NotifyHealthChanged();
}

void UCharacterAttributes::NotifyMaxHealthChanged()
{
	OnMaxHealthChanged.Broadcast(MaxHealth);
}

void UCharacterAttributes::OnRep_MaxHealth()
{
	// 客户端收到服务器同步过来的最大血量，通知 UI（血条百分比也依赖它）
	NotifyMaxHealthChanged();
}


// Called when the game starts
void UCharacterAttributes::BeginPlay()
{
	Super::BeginPlay();
	
	// ...
	
}
	

// Called every frame
void UCharacterAttributes::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// ...
}

