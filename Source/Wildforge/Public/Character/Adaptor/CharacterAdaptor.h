// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Character/Components/structs/AttackMontageData.h"
#include "Engine/DataAsset.h"

#include "CharacterAdaptor.generated.h"

class UAnimInstance;
class USkeletalMesh;

/**
 * 角色适配器：**一个角色一份**的资源集合。
 *
 * ## 它解决什么问题
 *
 * `APlayerCharacter`
 * 的能力都在独立组件里，每个组件的动画蒙太奇、再加上角色自己的
 * 骨骼网格体与动画蓝图，都是「角色专属」的资源。换一个角色如果逐项去改组件默认值，
 * 很容易漏配；本类把这些资源收进一个 `UDataAsset`。每新增一个角色只做一个
 * `CharacterAdaptor` 的**蓝图子类**，把它指定到**角色蓝图**的
 * `Character|Adaptor → Character Adaptor Class`
 * 槽位，不必再逐项改角色组件里的资源。
 *
 * ## 怎么用
 *
 * 1. Content Browser → 右键 → Blueprint Class → 在 All Classes 里搜索
 *    `CharacterAdaptor`（选它做父类）；
 * 2. 打开这个适配器蓝图，在**类默认值**里填网格体 / 动画蓝图 /
 * 各能力组件的蒙太奇；
 * 3. 打开该角色使用的角色蓝图（如 `BP_WildforgeCharacter`），在类默认值的
 *    `Character|Adaptor → Character Adaptor Class` 里选中上面的适配器蓝图；
 * 4. 每一端在 `APlayerCharacter::BeginPlay`
 * 里本地应用这些资源；服务器若在运行时
 *    换适配器，新的类会复制给所有客户端（含模拟代理）并重新应用。
 *
 * 角色蓝图上的属性是 `TSubclassOf<UCharacterAdaptor>`，所以可以直接接受适配器
 * **蓝图类**。数据全部放在蓝图类的默认值（CDO）里，各端读同一份，适配器本身
 * 不需要实例化。
 *
 * ## 复制与应用的注意事项（改这里之前先读）
 *
 *   * UE 5.7 的 `USkeletalMeshComponent` **不复制** `SkeletalMesh` /
 * `AnimClass`， 各能力组件的蒙太奇字段也没有
 * `Replicated`。所以适配器不能只在服务器应用：
 *     每个端都要在本地把同一份资源写进网格 / 组件，别人的客户端才看得到正确的
 *     外观与动画。角色上复制的是**适配器类**（`TSubclassOf`），不是资源本身。
 *   * 适配器类由角色蓝图的类默认值硬引用，所有端都会加载，不会出现
 *     「客户端上没有这个资源」的问题。
 *   * **网格体 / 动画蓝图留空 = 保留角色（蓝图）当前的默认值**，日志记
 * WARNING； 而**蒙太奇留空 =
 * 明确清空**。后者是刻意的：从「有翻滚动画的角色」切到
 *     「没有翻滚动画的角色」时，必须把旧角色的蒙太奇清掉，否则会继续播别人的动画。
 *     新增角色时如果确实没有某个蒙太奇，就让它留空，这是合法配置。
 *   * 本类**只放资源**。冷却、伤害、速度这类可调数值属于各组件自己的配置类 /
 * ini （见 CLAUDE.md 的强制约束 6），不要塞进适配器。
 */
UCLASS(BlueprintType, Blueprintable)
class WILDFORGE_API UCharacterAdaptor : public UDataAsset {
  GENERATED_BODY()

public:
  // ===== 网格体与动画蓝图 =====

  // 角色使用的骨骼网格体。留空 = 保留角色蓝图里配的网格体（记 WARNING）。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Mesh")
  TObjectPtr<USkeletalMesh> SkeletalMesh;

  // 角色使用的动画蓝图类。留空 = 保留角色蓝图里配的动画蓝图（记 WARNING）。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Mesh")
  TSubclassOf<UAnimInstance> AnimClass;

  // 是否用下面的 `MeshRelativeTransform` 覆盖网格体的相对变换。
  // 默认关：多数角色共用同一套骨架比例，只有网格体原点 /
  // 朝向不同的角色才需要打开。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Mesh")
  bool bOverrideMeshTransform = false;

  // 网格体相对角色根组件的变换（仅当 `bOverrideMeshTransform` 打开时生效）。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Mesh",
            meta = (EditCondition = "bOverrideMeshTransform"))
  FTransform MeshRelativeTransform = FTransform::Identity;
  
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Mesh")
  FName WouponSocketName = NAME_None;
  // ===== 攻击 =====

  // 角色默认（徒手）攻击连招蒙太奇列表（每项 = 一个蒙太奇 + 它的 Section 序列）。
  // 整个列表会覆盖 `UAttackComponent::AttackMontageList`（为空 = 清空）。
  // 装了武器后，`UWeaponAttackProfile` 会临时覆盖它；丢弃 / 清空武器后回到这里。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Attack")
  TArray<FAttackMontageData> AttackMontageList;

  // ===== 翻滚 =====

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|LandRoll")
  TObjectPtr<UAnimMontage> LandRollMontage;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|LandRoll")
  FName LandRollSectionName = NAME_None;

  // ===== 滑行 =====

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Slide")
  TObjectPtr<UAnimMontage> SlideMontage;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Slide")
  FName SlideSectionName = NAME_None;

  // ===== 加速 =====

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|SprintBoost")
  TObjectPtr<UAnimMontage> SprintMontage;

  // ===== 闪现（角色没有 `UBlinkComponent` 时会被跳过）=====

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Blink")
  TObjectPtr<UAnimMontage> BlinkMontage;

  // ===== 趴下（爬行）=====

  // 站着趴下
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Crawl")
  TObjectPtr<UAnimMontage> ProneFromStandMontage;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Crawl")
  FName ProneFromStandSectionName = NAME_None;

  // 奔跑趴下（可选，留空时组件回退到站立那一段）
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Crawl")
  TObjectPtr<UAnimMontage> ProneFromRunMontage;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Crawl")
  FName ProneFromRunSectionName = NAME_None;

  // 回滚站立（起身）
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Crawl")
  TObjectPtr<UAnimMontage> ProneToStandMontage;

  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character|Crawl")
  FName ProneToStandSectionName = NAME_None;
  // ===== 调试 =====

  // 一行资源快照（日志 / 调试用）：网格、动画蓝图、各蒙太奇的配置情况。
  UFUNCTION(BlueprintPure, Category = "Character|Adaptor",
            meta = (BlueprintThreadSafe))
  FString GetDebugString() const;
};
