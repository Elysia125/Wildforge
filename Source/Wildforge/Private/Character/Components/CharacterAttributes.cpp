// Fill out your copyright notice in the Description page of Project Settings.


#include "Character/Components/CharacterAttributes.h"

#include "Character/Settings/CharacterAttributesSettings.h"
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

	// 运行时数值：无条件下发（所有客户端可见），便于显示他人血条/等级。
	// 若只想让本人看到自己的精确数值，改成 DOREPLIFETIME_CONDITION(..., COND_OwnerOnly)。
	DOREPLIFETIME(UCharacterAttributes, MaxHealth);
	DOREPLIFETIME(UCharacterAttributes, Health);
	DOREPLIFETIME(UCharacterAttributes, MaxEnergy);
	DOREPLIFETIME(UCharacterAttributes, Energy);
	DOREPLIFETIME(UCharacterAttributes, Level);
	DOREPLIFETIME(UCharacterAttributes, Experience);
	DOREPLIFETIME(UCharacterAttributes, ExperienceToNextLevel);

	// 成长参数：角色生命周期内不变，只在出生束下发一次。
	// 客户端需要它们是为了用同一份曲线计算 GetMaxHealthForLevel /
	// GetExperienceRequiredForLevel，而不是去读本地 ini。
	DOREPLIFETIME_CONDITION(UCharacterAttributes, MaxLevel, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, BaseMaxHealth,
	                        COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, HealthPerLevel,
	                        COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, BaseMaxEnergy,
	                        COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, EnergyPerLevel,
	                        COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, BaseExperienceToNextLevel,
	                        COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, ExperienceGrowthFactor,
	                        COND_InitialOnly);

	// 精力自然回复参数：同样是生命周期内不变的调参，随出生束下发一次。
	// 客户端不推进回复（权威端 Tick 才会写 Energy），下发只是为了两端日志可对照。
	DOREPLIFETIME_CONDITION(UCharacterAttributes, EnergyRegenPerSecond,
	                        COND_InitialOnly);
	DOREPLIFETIME_CONDITION(UCharacterAttributes, EnergyRegenDelay,
	                        COND_InitialOnly);
}

void UCharacterAttributes::NotifyHealthChanged()
{
	OnHealthChanged.Broadcast(Health);
}

void UCharacterAttributes::NotifyMaxHealthChanged()
{
	OnMaxHealthChanged.Broadcast(MaxHealth);
}

void UCharacterAttributes::NotifyEnergyChanged()
{
	OnEnergyChanged.Broadcast(Energy);
}

void UCharacterAttributes::NotifyMaxEnergyChanged()
{
	OnMaxEnergyChanged.Broadcast(MaxEnergy);
}

void UCharacterAttributes::NotifyLevelChanged()
{
	OnLevelChanged.Broadcast(Level);
}

void UCharacterAttributes::NotifyExperienceChanged()
{
	OnExperienceChanged.Broadcast(Experience, ExperienceToNextLevel);
}

void UCharacterAttributes::NotifyAllAttributesChanged()
{
	NotifyMaxHealthChanged();
	NotifyHealthChanged();
	NotifyMaxEnergyChanged();
	NotifyEnergyChanged();
	NotifyLevelChanged();
	NotifyExperienceChanged();
}

void UCharacterAttributes::OnRep_Health()
{
	// 客户端收到服务器同步过来的血量，通知 UI
	NotifyHealthChanged();
}

void UCharacterAttributes::OnRep_MaxHealth()
{
	// 客户端收到服务器同步过来的最大血量，通知 UI（血条百分比也依赖它）
	NotifyMaxHealthChanged();
}

void UCharacterAttributes::OnRep_Energy()
{
	NotifyEnergyChanged();
}

void UCharacterAttributes::OnRep_MaxEnergy()
{
	NotifyMaxEnergyChanged();
}

void UCharacterAttributes::OnRep_Level()
{
	NotifyLevelChanged();
}

void UCharacterAttributes::OnRep_Experience()
{
	NotifyExperienceChanged();
}

void UCharacterAttributes::OnRep_ExperienceToNextLevel()
{
	// 经验与「下一级所需经验」是两条复制属性；无论哪条先到都刷新一次，
	// 另一条到达时会再刷一次，最终值一定一致。
	NotifyExperienceChanged();
}

void UCharacterAttributes::ApplyLevelStatGrowth(float OldMaxHealth,
                                                float OldMaxEnergy)
{
	const float NewMaxHealth = GetMaxHealthForLevel(Level);
	const float NewMaxEnergy = GetMaxEnergyForLevel(Level);

	// 升级时当前值跟着上限一起涨「新增的那部分」；降级时夹回新上限。
	const float HealthGain = FMath::Max(0.0f, NewMaxHealth - OldMaxHealth);
	const float EnergyGain = FMath::Max(0.0f, NewMaxEnergy - OldMaxEnergy);

	MaxHealth = NewMaxHealth;
	MaxEnergy = NewMaxEnergy;
	Health = FMath::Clamp(Health + HealthGain, 0.0f, MaxHealth);
	Energy = FMath::Clamp(Energy + EnergyGain, 0.0f, MaxEnergy);
}

void UCharacterAttributes::SetLevel(int32 InLevel)
{
	if (!IsAuthoritativeForActorComponent(this)) {
		WFLOG_ERROR(
		    "UCharacterAttributes::SetLevel 在非权威端被调用，已忽略；"
		    "客户端请改用对应的 Server_* RPC。");
		return;
	}

	const int32 NewLevel = FMath::Clamp(InLevel, 1, FMath::Max(1, MaxLevel));
	if (NewLevel == Level) {
		return;
	}

	const int32 OldLevel = Level;
	const float OldMaxHealth = MaxHealth;
	const float OldMaxEnergy = MaxEnergy;

	Level = NewLevel;
	ApplyLevelStatGrowth(OldMaxHealth, OldMaxEnergy);

	ExperienceToNextLevel = GetExperienceRequiredForLevel(Level);
	Experience = (ExperienceToNextLevel > 0)
	                 ? FMath::Clamp(Experience, 0, ExperienceToNextLevel - 1)
	                 : 0;

	NotifyLevelChanged();
	NotifyMaxHealthChanged();
	NotifyHealthChanged();
	NotifyMaxEnergyChanged();
	NotifyEnergyChanged();
	NotifyExperienceChanged();

	WFLOG_INFO(
	    "[等级] 属性组件（宿主 %s）设置等级：%d -> %d，生命上限=%.1f 精力上限=%.1f "
	    "升级经验=%d。",
	    GetOwner() ? *GetOwner()->GetName() : TEXT("None"), OldLevel, Level,
	    MaxHealth, MaxEnergy, ExperienceToNextLevel);
}

void UCharacterAttributes::AddExperience(int32 Amount)
{
	if (!IsAuthoritativeForActorComponent(this)) {
		WFLOG_ERROR(
		    "UCharacterAttributes::AddExperience 在非权威端被调用，已忽略；"
		    "客户端请改用对应的 Server_* RPC。");
		return;
	}

	if (Amount <= 0) {
		return;
	}

	if (Level >= MaxLevel) {
		// 满级后不再积累经验，经验条显示为满（ExperienceToNextLevel = 0）。
		Experience = 0;
		ExperienceToNextLevel = 0;
		NotifyExperienceChanged();
		WFLOG_INFO("[等级] 属性组件（宿主 %s）已满级（%d/%d），忽略 %d 点经验。",
		           GetOwner() ? *GetOwner()->GetName() : TEXT("None"), Level,
		           MaxLevel, Amount);
		return;
	}

	if (ExperienceToNextLevel <= 0) {
		ExperienceToNextLevel = GetExperienceRequiredForLevel(Level);
	}

	Experience += Amount;

	int32 LevelsGained = 0;
	while (Level < MaxLevel && ExperienceToNextLevel > 0 &&
	       Experience >= ExperienceToNextLevel) {
		const float OldMaxHealth = MaxHealth;
		const float OldMaxEnergy = MaxEnergy;

		Experience -= ExperienceToNextLevel;
		++Level;
		ExperienceToNextLevel = GetExperienceRequiredForLevel(Level);
		ApplyLevelStatGrowth(OldMaxHealth, OldMaxEnergy);
		++LevelsGained;

		WFLOG_INFO(
		    "[等级] 属性组件（宿主 %s）升级：%d -> %d，生命上限=%.1f 精力上限=%.1f "
		    "下一级所需经验=%d。",
		    GetOwner() ? *GetOwner()->GetName() : TEXT("None"), Level - 1, Level,
		    MaxHealth, MaxEnergy, ExperienceToNextLevel);
	}

	if (Level >= MaxLevel) {
		Experience = 0;
		ExperienceToNextLevel = 0;
	}

	if (LevelsGained > 0) {
		NotifyLevelChanged();
		NotifyMaxHealthChanged();
		NotifyHealthChanged();
		NotifyMaxEnergyChanged();
		NotifyEnergyChanged();
	}
	NotifyExperienceChanged();

	WFLOG_INFO(
	    "[等级] 属性组件（宿主 %s）获得 %d 经验：等级=%d/%d 经验=%d/%d（本次升级 "
	    "%d 级）。",
	    GetOwner() ? *GetOwner()->GetName() : TEXT("None"), Amount, Level,
	    MaxLevel, Experience, ExperienceToNextLevel, LevelsGained);
}

void UCharacterAttributes::ApplyGameplaySettingsOverrides()
{
	// 只有权威端读 ini：客户端认的是组件上复制下来的值（见
	// CharacterAttributesSettings.h）。组件里不能用 HasAuthority()
	// （AActor 的方法，写了直接 C3861）。
	if (!IsAuthoritativeForActorComponent(this)) {
		return;
	}

	const UCharacterAttributesSettings *Settings =
	    UCharacterAttributesSettings::Get();

	int32 Applied = 0;
	if (Settings == nullptr) {
		WFLOG_ERROR("[配置] 属性组件取不到 UCharacterAttributesSettings（CDO 为空），"
		            "本次全部使用组件类默认值。宿主 %s",
		            GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
	} else {
		// 顺序很重要：先确定等级上限，再把初始等级夹进范围内。
		if (Settings->bOverride_MaxLevel) {
			MaxLevel = FMath::Max(1, Settings->MaxLevel);
			++Applied;
		}
		if (Settings->bOverride_InitialLevel) {
			Level = Settings->InitialLevel;
			++Applied;
		}
		if (Settings->bOverride_InitialHealth) {
			BaseMaxHealth = FMath::Max(1.0f, Settings->InitialHealth);
			++Applied;
		}
		if (Settings->bOverride_InitialEnergy) {
			BaseMaxEnergy = FMath::Max(1.0f, Settings->InitialEnergy);
			++Applied;
		}
		if (Settings->bOverride_HealthPerLevel) {
			HealthPerLevel = FMath::Max(0.0f, Settings->HealthPerLevel);
			++Applied;
		}
		if (Settings->bOverride_EnergyPerLevel) {
			EnergyPerLevel = FMath::Max(0.0f, Settings->EnergyPerLevel);
			++Applied;
		}
		if (Settings->bOverride_EnergyRegenPerSecond) {
			EnergyRegenPerSecond =
			    FMath::Max(0.0f, Settings->EnergyRegenPerSecond);
			++Applied;
		}
		if (Settings->bOverride_EnergyRegenDelay) {
			EnergyRegenDelay =
			    FMath::Max(0.0f, Settings->EnergyRegenDelay);
			++Applied;
		}
		if (Settings->bOverride_BaseExperienceToNextLevel) {
			BaseExperienceToNextLevel =
			    FMath::Max(1, Settings->BaseExperienceToNextLevel);
			++Applied;
		}
		if (Settings->bOverride_ExperienceGrowthFactor) {
			ExperienceGrowthFactor =
			    FMath::Max(1.0f, Settings->ExperienceGrowthFactor);
			++Applied;
		}
	}

	// ini 是手写文本，面板 Clamp 拦不住越界值，这里统一夹紧。
	MaxLevel = FMath::Max(1, MaxLevel);
	Level = FMath::Clamp(Level, 1, MaxLevel);
	BaseMaxHealth = FMath::Max(1.0f, BaseMaxHealth);
	BaseMaxEnergy = FMath::Max(1.0f, BaseMaxEnergy);
	HealthPerLevel = FMath::Max(0.0f, HealthPerLevel);
	EnergyPerLevel = FMath::Max(0.0f, EnergyPerLevel);
	BaseExperienceToNextLevel = FMath::Max(1, BaseExperienceToNextLevel);
	ExperienceGrowthFactor = FMath::Max(1.0f, ExperienceGrowthFactor);
	EnergyRegenPerSecond = FMath::Max(0.0f, EnergyRegenPerSecond);
	EnergyRegenDelay = FMath::Max(0.0f, EnergyRegenDelay);

	// 出生时按等级曲线补满生命/精力，经验从 0 开始。
	MaxHealth = GetMaxHealthForLevel(Level);
	MaxEnergy = GetMaxEnergyForLevel(Level);
	Health = MaxHealth;
	Energy = MaxEnergy;
	Experience = 0;
	ExperienceToNextLevel = GetExperienceRequiredForLevel(Level);

	NotifyAllAttributesChanged();

	const FString Source =
	    (Settings == nullptr)
	        ? FString(TEXT("配置类缺失，全部用类默认值"))
	        : ((Applied > 0)
	               ? FString::Printf(TEXT("应用了 %d 项 ini 覆盖"), Applied)
	               : FString(TEXT("没有 ini 覆盖（全部用类默认值）")));
	WFLOG_INFO(
	    "[配置] 属性组件（宿主 %s，权威端）：%s；生效值 等级=%d/%d 生命=%.1f/%.1f "
	    "精力=%.1f/%.1f 每级生命=%.1f 每级精力=%.1f 升级经验=%d 经验成长=%.2f "
	    "精力回复=%.1f/s（消耗后延迟 %.2fs）。",
	    GetOwner() ? *GetOwner()->GetName() : TEXT("None"), *Source, Level,
	    MaxLevel, Health, MaxHealth, Energy, MaxEnergy, HealthPerLevel,
	    EnergyPerLevel, ExperienceToNextLevel, ExperienceGrowthFactor,
	    EnergyRegenPerSecond, EnergyRegenDelay);
}

// Called when the game starts
void UCharacterAttributes::BeginPlay()
{
	Super::BeginPlay();

	if (!IsAuthoritativeForActorComponent(this)) {
		// 客户端不读 ini、不初始化：等服务器把出生束复制过来。
		WFLOG_INFO(
		    "[属性] 客户端属性组件就绪（宿主 %s）：等级=%d 生命=%.1f/%.1f "
		    "精力=%.1f/%.1f 经验=%d/%d（来自服务器复制）。",
		    GetOwner() ? *GetOwner()->GetName() : TEXT("None"), Level, Health,
		    MaxHealth, Energy, MaxEnergy, Experience, ExperienceToNextLevel);
		return;
	}

	// 权威端：读配置 → 解析成长参数 → 按等级曲线初始化。
	ApplyGameplaySettingsOverrides();
}

// Called every frame
void UCharacterAttributes::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 精力自然回复：只在权威端推进，客户端只读复制下来的 Energy。
	// （客户端本地回复会与服务器分叉，也会让「精力不足」的判定变成客户端说了算。）
	if (!IsAuthoritativeForActorComponent(this) || DeltaTime <= 0.0f) {
		return;
	}
	if (EnergyRegenPerSecond <= 0.0f || Energy >= MaxEnergy) {
		return;
	}

	// 消耗后延迟：LastEnergyDecreaseTime 由 SetEnergy 在每次精力下降时刷新。
	// 加速持续扣费期间它会一直更新，所以冲刺时不会边扣边回；停下后等
	// EnergyRegenDelay 秒才开始回复。
	const UWorld *World = GetWorld();
	const float Now = (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
	if (Now - LastEnergyDecreaseTime < EnergyRegenDelay) {
		return;
	}

	AddEnergy(EnergyRegenPerSecond * DeltaTime);

	// 只在「刚好回满」的那一帧记一条：回满后 Tick 会在上面的早退分支直接返回，
	// 不会每帧刷日志。
	if (Energy >= MaxEnergy) {
		WFLOG_INFO("[属性] 精力已回满（%.1f/%.1f，每秒回复 %.1f）。宿主 %s",
		           Energy, MaxEnergy, EnergyRegenPerSecond,
		           GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
	}
}
