// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "Utils/WildforgeAuthority.h"
#include "Utils/WildforgeLog.h"

#include "CharacterAttributes.generated.h"

// 属性变化通知：服务器权威改动 与 客户端 RepNotify 都会广播，UI/蓝图可绑定
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, float, NewHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMaxHealthChanged, float,
                                            NewMaxHealth);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnergyChanged, float, NewEnergy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMaxEnergyChanged, float,
                                            NewMaxEnergy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelChanged, int32, NewLevel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnExperienceChanged, int32,
                                             NewExperience,
                                             int32,
                                             NewExperienceToNextLevel);

/**
 * 角色的基础属性：生命、精力、等级与经验。
 *
 * ## 权威与复制
 *
 * - 所有运行时数值都由服务器权威写入，客户端只读复制值；Setter 全部是
 *   `BlueprintAuthorityOnly`，并额外做 `IsAuthoritativeForActorComponent` 门禁。
 * - `Health` / `MaxHealth` / `Energy` / `MaxEnergy` / `Level` / `Experience` /
 *   `ExperienceToNextLevel` 正常复制（`ReplicatedUsing`），变化时广播委托。
 * - 成长参数（`BaseMaxHealth` / `HealthPerLevel` / ...）在权威端 `BeginPlay`
 *   从 `UCharacterAttributesSettings` 解析，然后用 `COND_InitialOnly` 下发；
 *   客户端用同一份参数调用 `GetMaxHealthForLevel` /
 *   `GetExperienceRequiredForLevel`，得到的曲线与服务器一致，**不需要读 ini**。
 * - 精力只在权威端自然回复：最后一次消耗后等待 `EnergyRegenDelay` 秒，
 *   再按 `EnergyRegenPerSecond` 每秒回复；客户端 Tick 不写 Energy。
 *
 * ## 等级曲线（不逐级配置，函数化）
 *
 *   - `MaxHealth(L) = BaseMaxHealth + HealthPerLevel * (L - 1)`
 *   - `MaxEnergy(L) = BaseMaxEnergy + EnergyPerLevel * (L - 1)`
 *   - `RequiredXP(L) = round(BaseExperienceToNextLevel
 *                            * ExperienceGrowthFactor ^ (L - 1))`
 *
 * 升级时生命/精力上限按曲线增长，当前值同步增加相同的差值（相当于升级回复
 * 新增的那部分上限）；降级时当前值会被夹到新的上限内。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent),
       BlueprintType, Blueprintable)
class WILDFORGE_API UCharacterAttributes : public UActorComponent {
  GENERATED_BODY()
private:
  // ===== 运行时数值（服务器权威，所有客户端可见）=====

  UPROPERTY(ReplicatedUsing = OnRep_MaxHealth, BlueprintGetter = GetMaxHealth,
            BlueprintSetter = SetMaxHealth, Category = "Attributes")
  float MaxHealth = 100.0f;

  UPROPERTY(ReplicatedUsing = OnRep_Health, BlueprintGetter = GetHealth,
            BlueprintSetter = SetHealth, Category = "Attributes")
  float Health = 100.0f;

  UPROPERTY(ReplicatedUsing = OnRep_MaxEnergy,
            BlueprintGetter = GetMaxEnergy, BlueprintSetter = SetMaxEnergy,
            Category = "Attributes")
  float MaxEnergy = 100.0f;

  UPROPERTY(ReplicatedUsing = OnRep_Energy, BlueprintGetter = GetEnergy,
            BlueprintSetter = SetEnergy, Category = "Attributes")
  float Energy = 100.0f;

  // ===== 精力自然回复（权威端推进，客户端只读复制值）=====

  /** 精力自然回复速度（每秒）；0 = 不自动回复。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float EnergyRegenPerSecond = 5.0f;

  /** 最后一次消耗精力后，延迟多久才开始回复（秒）。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float EnergyRegenDelay = 1.5f;

  /**
   * 最近一次精力下降的世界时间（仅权威端使用，不复制）。
   *
   * 每次扣精力都会刷新它：加速持续扣费期间它会一直更新，于是「回复延迟」
   * 天然覆盖「正在消耗时不要回复」，不需要 UCharacterAttributes 反过来
   * 依赖 SprintBoost / LandRoll 组件。
   */
  float LastEnergyDecreaseTime = -1000.0f;

  // 私有成员不能用 BlueprintReadOnly（UHT: "should not be used on private
  // members"），改用 BlueprintGetter 指向下面的 BlueprintPure 取值函数。
  UPROPERTY(ReplicatedUsing = OnRep_Level, BlueprintGetter = GetLevel,
            Category = "Attributes")
  int32 Level = 1;

  UPROPERTY(ReplicatedUsing = OnRep_Experience, BlueprintGetter = GetExperience,
            Category = "Attributes")
  int32 Experience = 0;

  UPROPERTY(ReplicatedUsing = OnRep_ExperienceToNextLevel,
            BlueprintGetter = GetExperienceToNextLevel, Category = "Attributes")
  int32 ExperienceToNextLevel = 100;

  // ===== 成长参数（权威端 BeginPlay 解析，COND_InitialOnly 下发）=====
  // 客户端也会收到这几个值，用于本地计算与服务器一致的曲线（只读，不写）。

  /** 等级上限：达到后不再获得经验。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  int32 MaxLevel = 100;

  /** 1 级时的生命上限（曲线起点）。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float BaseMaxHealth = 100.0f;

  /** 每升 1 级增加的生命上限。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float HealthPerLevel = 10.0f;

  /** 1 级时的精力上限（曲线起点）。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float BaseMaxEnergy = 100.0f;

  /** 每升 1 级增加的精力上限。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float EnergyPerLevel = 10.0f;

  /** 1 级升 2 级所需经验（曲线起点）。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  int32 BaseExperienceToNextLevel = 100;

  /** 每高 1 级，升级所需经验的增长倍率（1.0 = 每级固定）。 */
  UPROPERTY(Replicated, EditDefaultsOnly, Category = "Attributes|Progression")
  float ExperienceGrowthFactor = 1.15f;

  // 属性变化时广播；服务器由 Set 函数调用，客户端由 OnRep 调用
  void NotifyHealthChanged();
  void NotifyMaxHealthChanged();
  void NotifyEnergyChanged();
  void NotifyMaxEnergyChanged();
  void NotifyLevelChanged();
  void NotifyExperienceChanged();
  void NotifyAllAttributesChanged();

  /** 权威端 BeginPlay：读配置、解析成长参数并初始化当前属性。 */
  void ApplyGameplaySettingsOverrides();

  /** 等级已经改变后调用：按当前等级重算上限，并把当前值同步增减。 */
  void ApplyLevelStatGrowth(float OldMaxHealth, float OldMaxEnergy);

public:
  // Sets default values for this component's properties
  UCharacterAttributes();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  // UI / 蓝图绑定这些事件即可在对应属性变化时刷新
  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnHealthChanged OnHealthChanged;

  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnMaxHealthChanged OnMaxHealthChanged;

  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnEnergyChanged OnEnergyChanged;

  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnMaxEnergyChanged OnMaxEnergyChanged;

  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnLevelChanged OnLevelChanged;

  UPROPERTY(BlueprintAssignable, Category = "Attributes")
  FOnExperienceChanged OnExperienceChanged;

private:
  UFUNCTION()
  void OnRep_Health();

  UFUNCTION()
  void OnRep_MaxHealth();

  UFUNCTION()
  void OnRep_Energy();

  UFUNCTION()
  void OnRep_MaxEnergy();

  UFUNCTION()
  void OnRep_Level();

  UFUNCTION()
  void OnRep_Experience();

  UFUNCTION()
  void OnRep_ExperienceToNextLevel();

protected:
  // Called when the game starts
  virtual void BeginPlay() override;

public:
  // Called every frame
  virtual void
  TickComponent(float DeltaTime, ELevelTick TickType,
                FActorComponentTickFunction *ThisTickFunction) override;

  // ===== 生命 =====

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetHealthPercent() const {
    return MaxHealth > KINDA_SMALL_NUMBER ? Health / MaxHealth : 0.0f;
  }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetHealth() const { return Health; }

  // 以下 setter 都是 BlueprintAuthorityOnly：客户端直接调用会被引擎静默丢弃
  // （且 BlueprintSetter 形式也一样），这里再加一道开发期门禁，让误用立刻可见。
  // 注意用组件版判断：HasAuthority() 是 AActor 的方法，组件上不存在。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetHealth(float InHealth) {
    if (!IsAuthoritativeForActorComponent(this)) {
      WFLOG_ERROR(
          "UCharacterAttributes::SetHealth 在非权威端被调用，已忽略；"
          "客户端请改用对应的 Server_* RPC。");
      return;
    }
    const float Clamped = FMath::Clamp(InHealth, 0.0f, FMath::Max(0.0f, MaxHealth));
    if (Health != Clamped) {
      Health = Clamped;
      NotifyHealthChanged();
    }
  }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void AddHealth(float InHealth) { SetHealth(Health + InHealth); }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetMaxHealth() const { return MaxHealth; }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetMaxHealth(float InHealth) {
    if (!IsAuthoritativeForActorComponent(this)) {
      WFLOG_ERROR(
          "UCharacterAttributes::SetMaxHealth 在非权威端被调用，已忽略；"
          "客户端请改用对应的 Server_* RPC。");
      return;
    }
    const float Clamped = FMath::Max(1.0f, InHealth);
    if (MaxHealth != Clamped) {
      MaxHealth = Clamped;
      NotifyMaxHealthChanged();
      if (Health > MaxHealth) {
        Health = MaxHealth;
        NotifyHealthChanged();
      }
    }
  }

  // ===== 精力 =====

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetEnergyPercent() const {
    return MaxEnergy > KINDA_SMALL_NUMBER ? Energy / MaxEnergy : 0.0f;
  }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetEnergy() const { return Energy; }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetEnergy(float InEnergy) {
    if (!IsAuthoritativeForActorComponent(this)) {
      WFLOG_ERROR(
          "UCharacterAttributes::SetEnergy 在非权威端被调用，已忽略；"
          "客户端请改用对应的 Server_* RPC。");
      return;
    }
    const float Clamped = FMath::Clamp(InEnergy, 0.0f, FMath::Max(0.0f, MaxEnergy));
    if (Energy != Clamped) {
      // 记录「最近一次消耗」的时间：Tick 里的自然回复会等 EnergyRegenDelay
      // 之后才开始。加速每帧扣费会不断刷新它，所以加速期间不会边扣边回。
      if (Clamped < Energy) {
        const UWorld *World = GetWorld();
        LastEnergyDecreaseTime =
            (World != nullptr) ? World->GetTimeSeconds() : 0.0f;
      }
      Energy = Clamped;
      NotifyEnergyChanged();
    }
  }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void AddEnergy(float InEnergy) { SetEnergy(Energy + InEnergy); }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetMaxEnergy() const { return MaxEnergy; }

  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetMaxEnergy(float InEnergy) {
    if (!IsAuthoritativeForActorComponent(this)) {
      WFLOG_ERROR(
          "UCharacterAttributes::SetMaxEnergy 在非权威端被调用，已忽略；"
          "客户端请改用对应的 Server_* RPC。");
      return;
    }
    const float Clamped = FMath::Max(1.0f, InEnergy);
    if (MaxEnergy != Clamped) {
      MaxEnergy = Clamped;
      NotifyMaxEnergyChanged();
      if (Energy > MaxEnergy) {
        Energy = MaxEnergy;
        NotifyEnergyChanged();
      }
    }
  }

  // ===== 等级与经验 =====

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  int32 GetLevel() const { return Level; }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  int32 GetExperience() const { return Experience; }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  int32 GetExperienceToNextLevel() const { return ExperienceToNextLevel; }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetExperiencePercent() const {
    if (ExperienceToNextLevel <= 0) {
      return 1.0f;
    }
    return FMath::Clamp(static_cast<float>(Experience) /
                            static_cast<float>(ExperienceToNextLevel),
                        0.0f, 1.0f);
  }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  int32 GetMaxLevel() const { return MaxLevel; }

  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  bool IsMaxLevel() const { return Level >= MaxLevel; }

  /** 等级 L 的生命上限（服务器与客户端用同一份已下发的成长参数计算）。 */
  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetMaxHealthForLevel(int32 InLevel) const {
    const int32 ClampedLevel =
        FMath::Clamp(InLevel, 1, FMath::Max(1, MaxLevel));
    return FMath::Max(
        1.0f, BaseMaxHealth +
                  HealthPerLevel * static_cast<float>(ClampedLevel - 1));
  }

  /** 等级 L 的精力上限。 */
  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  float GetMaxEnergyForLevel(int32 InLevel) const {
    const int32 ClampedLevel =
        FMath::Clamp(InLevel, 1, FMath::Max(1, MaxLevel));
    return FMath::Max(
        1.0f, BaseMaxEnergy +
                  EnergyPerLevel * static_cast<float>(ClampedLevel - 1));
  }

  /** 从等级 InLevel 升到下一级所需经验；满级返回 0。
   *  曲线：round(BaseExperienceToNextLevel *
   *              ExperienceGrowthFactor ^ (InLevel - 1))。 */
  UFUNCTION(BlueprintPure, Category = "Attributes",
            meta = (BlueprintThreadSafe))
  int32 GetExperienceRequiredForLevel(int32 InLevel) const {
    if (MaxLevel <= 1 || InLevel >= MaxLevel) {
      return 0;
    }
    const int32 ClampedLevel = FMath::Max(1, InLevel);
    const double Growth = FMath::Max(1.0f, ExperienceGrowthFactor);
    const double Raw =
        static_cast<double>(BaseExperienceToNextLevel) *
        FMath::Pow(Growth, static_cast<double>(ClampedLevel - 1));
    return FMath::Max(1, FMath::RoundToInt(Raw));
  }

  /** 直接设置等级（调试/读档用）。会按曲线重算生命/精力上限。 */
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void SetLevel(int32 InLevel);

  /** 增加经验；可能连续升多级。满级后经验归零并显示满条。 */
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Attributes")
  void AddExperience(int32 Amount);
};
