#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "AnimNotifyCombo.generated.h"

UCLASS()
class WILDFORGE_API UAnimNotify_Combo : public UAnimNotify
{
    GENERATED_BODY()

public:
    // 重写 Notify 函数
    virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference) override;
};