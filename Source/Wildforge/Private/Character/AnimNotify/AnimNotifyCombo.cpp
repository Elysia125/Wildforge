// AnimNotify_Combo.cpp
#include "Character/AnimNotify/AnimNotifyCombo.h"

#include "Character/Components/AttackComponent.h" // 你的攻击组件头文件
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Utils/WildforgeLog.h"

void UAnimNotify_Combo::Notify(
    USkeletalMeshComponent *MeshComp, UAnimSequenceBase *Animation,
    const FAnimNotifyEventReference &EventReference) {
  Super::Notify(MeshComp, Animation, EventReference);

  if (!MeshComp || !MeshComp->GetOwner())
    return;

  // 拿到角色上的攻击组件
  if (UAttackComponent *AttackComp =
          MeshComp->GetOwner()->FindComponentByClass<UAttackComponent>()) {
    // 🔴 网络同步关键：连击窗口是**玩法状态**，只允许服务器开。
    // 之前这里直接写 `AttackComp->bCanCombo = true`：蒙太奇现在会被同步到所有端
    // （Multicast），客户端也跑这个通知，于是客户端会改自己那份副本——服务器那份
    // 始终是 false，连击在联机下直接失效（单机看不出来）。
    // 现在统一走 Server RPC：服务器上就地执行，单机/联机同一条路径。
    //
    // 这条日志的作用：如果攻击动画播到这一帧却没有这条日志，说明
    //   * 蒙太奇里根本没挂这个通知（Notify 类不是 UAnimNotify_Combo），或
    //   * 蒙太奇压根没在播（起手就被拒了）。
    WFLOG_INFO("[攻击] AnimNotify_Combo 触发（宿主 %s，本端权威=%d）→ 发 "
               "Server_NotifyComboWindow",
               *MeshComp->GetOwner()->GetName(),
               MeshComp->GetOwner()->HasAuthority() ? 1 : 0);
    AttackComp->Server_NotifyComboWindow();
  } else {
    WFLOG_WARNING("[攻击] AnimNotify_Combo 触发，但宿主 %s 上没有 UAttackComponent，"
                  "连击窗口不会开。",
                  *MeshComp->GetOwner()->GetName());
  }
}
