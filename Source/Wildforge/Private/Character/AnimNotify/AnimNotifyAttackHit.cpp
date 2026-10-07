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
        // 🔴 网络同步关键：伤害判定必须在服务器执行。
        // PerformDamageTrace 现在是 BlueprintAuthorityOnly + 内部还有一道
        // 「非权威端拒绝」门禁，所以在客户端调用只会白跑一趟（并记一条错误日志），
        // 这里先判一次是为了少绕这一趟；真正的权威保证在组件内部。
        if (MeshComp->GetOwner()->HasAuthority())
        {
            AttackComp->PerformDamageTrace(); // 在组件里写好的射线检测/伤害判定
        }

        // 表现层（特效、音效）在本地直接放：
        // 蒙太奇已被 Multicast_PlayAttackMontage 同步到所有端，所以每个端都会命中
        // 这个通知，各自播一次特效即可——不需要再为表现层多发一轮 RPC。
        AttackComp->PlayAttackEffects();
    }
}
