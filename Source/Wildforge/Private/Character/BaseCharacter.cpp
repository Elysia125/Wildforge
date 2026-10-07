// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/BaseCharacter.h"

#include "Components/InputComponent.h"
#include "Utils/WildforgeLog.h"

// Sets default values
ABaseCharacter::ABaseCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	// 角色参与网络复制，挂在上面的组件（背包 / 攻击 / 加速 / 闪现…）才能复制。
	// 少了这一行，组件的 DOREPLIFETIME_* 全部形同虚设。
	bReplicates = true;

	CharacterAttributes =
	    CreateDefaultSubobject<UCharacterAttributes>(TEXT("CharacterAttributes"));

	// 这里刻意不打日志：构造函数会为 CDO 与每个蓝图默认对象各跑一次，
	// 每次生成角色都刷一遍没有信息量。生命周期日志放 BeginPlay。
}

// Called when the game starts or when spawned
void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 本类不再持有任何能力状态：加速 / 闪现的基准值各自在
	// USprintBoostComponent / UBlinkComponent 里按需抓取（那里才拿得到正确的时机）。
	WFLOG_INFO("ABaseCharacter %s BeginPlay：本端权威=%d。", *GetName(),
	           HasAuthority() ? 1 : 0);
}

// Called every frame
void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

// Called to bind functionality to input
void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

}
