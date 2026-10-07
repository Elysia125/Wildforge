// AnimNotify_AttackHit.cpp
#include "Character/AnimNotify/AnimNotifyAttackHit.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Character/Components/AttackComponent.h" // 你的攻击组件头文件

void UAnimNotify_AttackHit::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
    Super::Notify(MeshComp, Animation, EventReference);

    if (!MeshComp || !MeshComp->GetOwner()) return;

    // 拿到角色上的攻击组件
    if (UAttackComponent* AttackComp = MeshComp->GetOwner()->FindComponentByClass<UAttackComponent>())
    {
        // 🔴 网络同步关键：伤害判定必须在服务器执行
        if (MeshComp->GetOwner()->HasAuthority())
        {
            AttackComp->PerformDamageTrace(); // 在组件里写好的射线检测/伤害判定
        }
        
        // 表现层（特效、音效）可以在客户端多播
        AttackComp->PlayAttackEffects(); 
    }
}