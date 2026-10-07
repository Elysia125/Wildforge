// AnimNotify_AttackHit.h
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotifyAttackHit.generated.h"

UCLASS()
class WILDFORGE_API UAnimNotify_AttackHit : public UAnimNotify
{
    GENERATED_BODY()

public:
    // 重写 Notify 函数
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};