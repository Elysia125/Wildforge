// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Character/Components/structs/AttackMontageData.h"
#include "Engine/DataAsset.h"

#include "WeaponAttackProfile.generated.h"

/**
 * 武器攻击配置：**一件武器一份**的招式表。
 *
 * ## 为什么把它单独做成资产，而不是给武器挂 `UAttackComponent`
 *
 * 攻击权威必须留在角色身上：
 *   * 客户端发往武器的 `Server_*` RPC 需要 owning connection（关卡里摆放的武器
 *     actor 没有 PlayerController owner，RPC 会被引擎静默丢弃，见 bug-093）；
 *   * 蒙太奇必须播在角色自己的 `AnimInstance` 上（`PlayAttackMontageInternal`
 *     要求宿主是 `ACharacter`），伤害轨迹也从角色位置/朝向发出。
 *
 * 所以武器与角色的关系只是「换一张招式表」：拾取时把本**配置类**交给角色攻击
 * 组件的 `SetWeaponAttackProfile()`，攻击组件其余逻辑（连击、冷却、伤害、移动锁）
 * 完全不用改。
 *
 * ## 用法（与 `UCharacterAdaptor` 同一套约定）
 *
 * 1. Content Browser → 右键 → Blueprint Class → 在 All Classes 里搜索
 *    `WeaponAttackProfile`（选它做父类），得到一个**蓝图类资产**
 *    （例如 `DA_SpearAttackProfile`）；
 * 2. 打开这个配置蓝图，在**类默认值**的 `AttackMontageList` 里填招式，
 *    结构与角色适配器的 `UCharacterAdaptor::AttackMontageList` 完全一样：
 *    每项 = 一个蒙太奇 + `SectionNames` 序列，连击按「列表顺序 → Section 顺序」推进；
 * 3. 在武器蓝图（如 `BP_SpearPickup`）里加一个变量，类型选
 *    `WeaponAttackProfile` 的 **Class Reference**（不是 Object Reference！），
 *    默认值指向第 1 步的配置蓝图类；
 * 4. 拾取成功的**服务器分支**里，对角色攻击组件调用
 *    `SetWeaponAttackProfile(ProfileClass)`；丢弃 / 切换武器时调用
 *    `ClearWeaponAttackProfile()`。
 *
 * 数据不在实例上，而是在配置蓝图类的 CDO（类默认值）里；各端读同一份，
 * 配置本身永不实例化。这也是为什么 `UAttackComponent::WeaponAttackProfile`
 * 的类型是 `TSubclassOf<UWeaponAttackProfile>`：`TObjectPtr` 属性选不到蓝图类资产。
 *
 * ## 语义
 *
 *   * 类引用为空（徒手 / 已清空）= 回退到角色适配器的 `AttackMontageList`；
 *   * 引用了配置但列表为空 = 同样回退，并记一条 WARNING（空列表大概率是配置漏了，
 *     静默变成「不能攻击」更难排查）。
 */
UCLASS(BlueprintType, Blueprintable)
class WILDFORGE_API UWeaponAttackProfile : public UDataAsset {
  GENERATED_BODY()

public:
  // 攻击连招蒙太奇列表（每项 = 一个蒙太奇 + 它的 Section 序列）。
  // 非空时**覆盖**角色适配器里的默认招式；为空时回退到默认招式。
  UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Attack")
  TArray<FAttackMontageData> AttackMontageList;
};
