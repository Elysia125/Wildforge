// AnimNotify_Combo.cpp
#include "Character/AnimNotify/AnimNotifyCombo.h"

#include "Character/Components/AttackComponent.h" // 你的攻击组件头文件
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"


void UAnimNotify_Combo::Notify(
    USkeletalMeshComponent *MeshComp, UAnimSequenceBase *Animation,
    const FAnimNotifyEventReference &EventReference) {
  Super::Notify(MeshComp, Animation, EventReference);

  if (!MeshComp || !MeshComp->GetOwner())
    return;

  // 拿到角色上的攻击组件
  if (UAttackComponent *AttackComp =
          MeshComp->GetOwner()->FindComponentByClass<UAttackComponent>()) {
    AttackComp->bCanCombo = true; // 允许连击
  }
}