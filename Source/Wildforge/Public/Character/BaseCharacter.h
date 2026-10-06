// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Character/Components/CharacterAttributes.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"

#include "BaseCharacter.generated.h"


UCLASS(ClassGroup = (Custom),BlueprintType, Blueprintable)
class WILDFORGE_API ABaseCharacter : public ACharacter {
  GENERATED_BODY()
public:
  // Sets default values for this character's properties
  ABaseCharacter();

private:
  UPROPERTY(BlueprintGetter = GetCharacterAttributes, Category = "Attributes")
  TObjectPtr<UCharacterAttributes> CharacterAttributes;

  // ===== 速度提升（长按加速）的内部状态 =====
  // 全部只在权威端写入：客户端通过 CharacterMovement 的 MaxWalkSpeed 复制看到结果。
  struct FSprintBoostState {
    // 本次提升的起始速度（权威端开始提升那一刻的 MaxWalkSpeed）
    float StartSpeed = 0.0f;
    // 本次提升的目标速度
    float TargetSpeed = 0.0f;

    // 未被加速污染的基准 MaxWalkSpeed / MaxWalkSpeedCrouched，重置时精确回到它们
    float BaseMaxWalkSpeed = 0.0f;
    float BaseMaxWalkSpeedCrouched = 0.0f;
    // 蹲伏速度与步行速度的基准比例（BaseMaxWalkSpeedCrouched / BaseMaxWalkSpeed）：
    // BeginPlay 抓一次后固定。加速时按这个比例同步缩放蹲伏速度，而不是把蹲伏抬到和步行一样快。
    float CrouchSpeedRatio = 1.0f;

    // 提升已经持续的时间（秒）；进度 = ElapsedTime / BoostDuration
    float ElapsedTime = 0.0f;
    // 从起始速度线性升到目标速度需要的总时间（秒）
    float BoostDuration = 1.0f;
    // 上一次刷新时的世界时间（秒），用来算两次刷新之间的真实时间增量
    float LastUpdateTime = 0.0f;

    // 刷新用的定时器句柄（间隔模式是循环定时器，每帧模式是 next-tick 定时器）
    FTimerHandle TimerHandle;

    // 每次 StartSpeedBoost 自增的「代次」。OnSpeedBoostUpdated 广播时蓝图可能重开/重置加速，
    // 定时器回调续挂 next-tick 之前靠它确认自己这一代还没被替换掉。
    uint32 Generation = 0;

    // 是否处于「加速中」——含已经到顶、定时器已停但速度仍保持的状态，直到 ResetMaxSpeed
    bool bBoostActive = false;
    // 刷新方式：true = 固定间隔的循环定时器；false = 每帧一次的 next-tick 链
    bool bIntervalTimer = false;
  };

  FSprintBoostState SprintBoost;

  // 定时器回调：按 ElapsedTime / BoostDuration 把 MaxWalkSpeed 从起始值插值到目标值，
  // 到顶即停表（不再空转），但速度保持到 ResetMaxSpeed 为止。
  // 每帧模式（bIntervalTimer == false）会在末尾重新挂一次 next-tick 定时器。
  void TickSprintBoost();
  // 只清定时器与刷新方式标志，不动 bBoostActive（到顶时需要保留「加速中」状态）
  void ClearSprintBoostTimer();
  // 把当前 MaxWalkSpeed / MaxWalkSpeedCrouched 记为基准值，并算出蹲伏比例（仅在未加速时调用才有意义）
  void CaptureBaseMaxSpeed();
  // 权威端专用：把步行速度写到移动组件，并让蹲伏速度按基准比例跟随缩放
  void ApplyMaxWalkSpeed(float InMaxWalkSpeed, float Alpha);

protected:
  // Called when the game starts or when spawned
  virtual void BeginPlay() override;

public:
  // Called every frame
  virtual void Tick(float DeltaTime) override;

  // Called to bind functionality to input
  virtual void SetupPlayerInputComponent(
      class UInputComponent *PlayerInputComponent) override;

  UFUNCTION(BlueprintPure, Category = "Attributes",meta = (BlueprintThreadSafe))
  UCharacterAttributes* GetCharacterAttributes() const {
    return CharacterAttributes.Get();
  }

  // ===== 加速状态查询（纯读，客户端可安全调用）=====

  // 是否正处于加速中（定时器在跑，或已到顶但尚未重置）
  UFUNCTION(BlueprintPure, Category = "Character|SpeedBoost",
            meta = (BlueprintThreadSafe))
  bool IsSpeedBoostActive() const;

  // 加速进度 0..1（0 = 刚起步，1 = 已到达目标速度）；未加速时为 0
  UFUNCTION(BlueprintPure, Category = "Character|SpeedBoost",
            meta = (BlueprintThreadSafe))
  float GetSpeedBoostAlpha() const;

  // 当前（未加速时的）基准 MaxWalkSpeed，也就是 ResetMaxSpeed 会回到的值
  UFUNCTION(BlueprintPure, Category = "Character|SpeedBoost",
            meta = (BlueprintThreadSafe))
  float GetBaseMaxWalkSpeed() const { return SprintBoost.BaseMaxWalkSpeed; }

  // 加速过程中每次刷新速度都会执行；服务器与客户端（速度复制后）都会触发。
  // 客户端可在这里做 FOV / 冲刺特效，不要在这里改速度相关的权威状态。
  UFUNCTION(BlueprintImplementableEvent, Category = "Character|SpeedBoost")
  void OnSpeedBoostUpdated(float CurrentMaxSpeed, float Alpha);

  // ===== 以下为服务器权威函数（BlueprintAuthorityOnly）=====
  // 它们没有 RPC 转发能力：客户端直接调会被引擎静默丢弃（callspace = Absorbed），
  // 所以这里再加一道开发期门禁，让误用立刻可见（与 UCharacterAttributes 同款写法）。
  // 客户端请改调下方对应的 Server_* RPC。

  // 长按加速：在 HoldThreshold 秒内由定时器把 MaxWalkSpeed 从当前值逐渐提升到
  // TargetMaxSpeed，BoostDuration 秒后到达（线性插值）。到达后速度保持，直到
  // ResetMaxSpeed（或再次调用本函数改为新的目标）。
  //
  // 参数：
  //   HoldThreshold   长按阈值（秒）。纯服务器侧参数，不参与速度计算——调用方需要先
  //                   自己等满这个时长再调用，或用它做节流，保证「点按」不会误触发加速。
  //   TargetMaxSpeed  加速后的最大速度（cm/s），必须 > 0
  //   BoostDuration   从起始速度到达 TargetMaxSpeed 需要的时间（秒），必须 > 0
  //   BoostInterval   定时器刷新间隔（秒）。0（默认）= 每帧刷新一次（内部用
  //                   SetTimerForNextTick 链，与帧率同步、最平滑）；正数 = 固定间隔刷新，
  //                   帧率波动时曲线更可预测。
  //                   ⚠️ 不能直接把 0 交给 FTimerManager::SetTimer：引擎对 InRate <= 0 的处理是
  //                   「清掉该句柄上的定时器」（TimerManager.h:157、TimerManager.cpp:617-659），
  //                   传 0 的结果是定时器根本不存在、一次都不会触发。
  //
  // 返回是否真的开始了本次加速（参数非法或已经在加速中会返回 false 并记一条 WFLOG_WARNING）。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Character|SpeedBoost")
  bool StartSpeedBoost(float HoldThreshold, float TargetMaxSpeed,
                       float BoostDuration = 1.5f, float BoostInterval = 0.0f);

  // 重置最大速度：清掉加速定时器，并把 MaxWalkSpeed / MaxWalkSpeedCrouched 精确还原到
  // 「加速之前」的基准值（SprintBoost.BaseMaxWalkSpeed / BaseMaxWalkSpeedCrouched）。
  // 未加速时调用是安全的空操作。加速开始时（StartSpeedBoost 内）也会调用它，
  // 避免重入时把加速后的速度误记成基准值。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Character|SpeedBoost")
  void ResetMaxSpeed();

  // 服务器侧改写「基准速度」（例如装备/疲劳等系统永久改变了角色速度）：
  // 未被加速污染时才允许写入，加速进行中调用会记一条 WFLOG_WARNING 并忽略。
  // InBaseMaxWalkSpeedCrouched < 0（默认）表示蹲伏基准按原有比例（CrouchSpeedRatio）推算；
  // 显式传入时会用新的「蹲伏/步行」比值更新这个比例。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Character|SpeedBoost")
  void SetBaseMaxSpeed(float InBaseMaxWalkSpeed,
                       float InBaseMaxWalkSpeedCrouched = -1.0f);

  // 点按闪现：沿角色视线水平方向做一次带碰撞扫描的瞬移。
  //
  // 行为：
  //   - 方向取 GetActorEyesViewPoint 的水平分量（忽略俯仰），所以朝上不会顶到天花板、
  //     朝下不会钻进地面；没有 Controller 时退回角色自身前向。
  //   - 全程用胶囊体做 Sweep：撞墙/撞障碍就停在障碍前（不会穿墙），或直接失败返回 false。
  //   - 落点校验：高于最大可站立坡度、或离地悬空（会直接掉下去）的落点一律判定失败，
  //     避免一下闪到墙外或半空。
  //   - 结束时把速度清零（可选保留水平速度），防止闪现后角色被旧速度带着继续滑。
  //
  // 参数（注意：输出引用必须排在带默认值的参数之前，否则编译器会报
  // "missing default argument on parameter 'OutLandingLocation'"，这是 C++ 规则，
  // UHT 不会替你发现）：
  //   OutLandingLocation 闪现落点（供特效/调试用）
  //   Distance       闪现距离（cm）
  //   MaxDistance    服务器侧允许的最大距离（cm），即使 RPC 传了更大的值也只会取它
  //   bKeepVelocity  true = 保留原有水平速度（手感更连续），false = 落地急停
  //   TraceChannel   扫描用的碰撞通道。注意别用胶囊体自身会阻塞的通道（默认 Pawn 会撞到自己）
  //
  // 返回是否真的完成了位移。
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Character|Blink")
  bool BlinkForward(FVector &OutLandingLocation, float Distance,
                    float MaxDistance = 1200.0f, bool bKeepVelocity = false,
                    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility);

  // ===== 客户端 -> 服务器：冲刺 / 闪现请求 =====
  // 输入绑定通常写在蓝图（模板的 Enhanced Input）里：判定「长按」后发
  // Server_StartSpeedBoost，判定「点按」后发 Server_Blink，松开 Shift 发
  // Server_StopSpeedBoost。C++ 侧不重复绑定输入，避免与蓝图绑定叠加。
  // _Validate 只做廉价参数检查；真正的范围收敛在权威函数内部完成。

  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Character|RPC")
  void Server_StartSpeedBoost(float HoldThreshold, float TargetMaxSpeed,
                              float BoostDuration = 1.5f,
                              float BoostInterval = 0.0f);

  // 松开 Shift：还原最大速度（未加速时调用无害，可放心重复发）
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Character|RPC")
  void Server_StopSpeedBoost();

  // 点按 Shift：请求闪现。距离由客户端提出、服务器收敛到 MaxDistance 以内
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Character|RPC")
  void Server_BlinkForward(float Distance, float MaxDistance = 1200.0f,
                           bool bKeepVelocity = false);

  // 按服务器上配置的最大距离（DefaultMaxBlinkDistance）闪现，客户端不需要自己抬参数
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Character|RPC")
  void Server_Blink();

  // 闪现允许的最大距离（cm）：Server_Blink 用它，Server_BlinkForward 也用它兜底
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Character|Blink",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
  float DefaultMaxBlinkDistance = 1200.0f;
};
