// Fill out your copyright notice in the Description page of Project Settings.

#include "Character/Player/PlayerCharacter.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Utils/WildforgeLog.h"

APlayerCharacter::APlayerCharacter() : ABaseCharacter() {
  Inventory = CreateDefaultSubobject<UPlayerInventory>(TEXT("Inventory"));
  Inventory->InitializeContainer(30);
  AttackComponent = CreateDefaultSubobject<UAttackComponent>(TEXT("AttackComponent"));
}

void APlayerCharacter::BeginPlay() {
  Super::BeginPlay();

  // 订阅攻击组件的两个通知。
  // 判断「当前是否已经绑过」再绑，避免蓝图里重复调用 BeginPlay 之类的场景重复注册
  // （动态多播内部本来也会按对象+函数去重，这里只是显式表达意图）。
  if (AttackComponent == nullptr) {
    WFLOG_WARNING("%s 没有 AttackComponent，攻击期间的移动门控不会生效。",
                  *GetName());
    return;
  }

  AttackComponent->OnAttackStarted.RemoveDynamic(
      this, &APlayerCharacter::HandleAttackStarted);
  AttackComponent->OnAttackStarted.AddDynamic(
      this, &APlayerCharacter::HandleAttackStarted);

  AttackComponent->OnAttackFinished.RemoveDynamic(
      this, &APlayerCharacter::HandleAttackFinished);
  AttackComponent->OnAttackFinished.AddDynamic(
      this, &APlayerCharacter::HandleAttackFinished);
}

// 攻击开始 → 禁止移动
void APlayerCharacter::HandleAttackStarted() {
  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return;
  }

  // 已经开始禁用了就不重复做：重复调用会把 MOVE_None 当成「攻击前的移动模式」
  // 存下来，攻击结束后就恢复不回去了（下面一段的判断就是防这个）。
  if (Movement->MovementMode == MOVE_None) {
    return;
  }

  // 记下攻击前的移动模式，攻击结束时原样还回去（走路/下落/飞行各自对应）
  MovementModeBeforeAttack = Movement->MovementMode;

  // 加速（Shift 长按）与攻击互斥：攻击期间把最大速度还原到基准值，
  // 免得攻击结束后角色带着加速状态继续滑。
  // ResetMaxSpeed 是 BlueprintAuthorityOnly + 内部有非权威端门禁，
  // 客户端调用只会在日志里留一条错误，所以这里先自己判权威端。
  if (HasAuthority() && IsSpeedBoostActive()) {
    ResetMaxSpeed();
  }

  // 禁止移动：MOVE_None 会让移动组件不再积分速度（输入照旧进来但不生效）。
  // 引擎不会替你清速度，所以显式停一次，避免攻击结束后带着旧速度滑出去。
  Movement->StopMovementImmediately();
  Movement->DisableMovement();

  WFLOG_INFO("[攻击] %s 攻击开始：禁止移动（原移动模式 %d，本端权威=%d）。",
             *GetName(), static_cast<int32>(MovementModeBeforeAttack.GetValue()),
             HasAuthority() ? 1 : 0);
}

// 攻击结束 → 恢复移动
void APlayerCharacter::HandleAttackFinished() {
  UCharacterMovementComponent *Movement = GetCharacterMovement();
  if (Movement == nullptr) {
    return;
  }

  // 不在「禁止移动」状态就说明本次结束回调与开始回调没有配对（例如组件在攻击中
  // 被销毁重建、或通知在客户端与服务器上到达次数不一致）——此时不动移动模式，
  // 否则会把角色从一个正常的移动模式里硬拽出来。
  if (Movement->MovementMode != MOVE_None) {
    return;
  }

  // 恢复攻击前的移动模式。注意 MOVE_Falling 是瞬时状态，落地那一帧移动组件
  // 自己会切回 Walking，所以这里直接还原也不会把人卡在空中。
  EMovementMode RestoreMode = MovementModeBeforeAttack.GetValue();
  if (RestoreMode == MOVE_None) {
    // 兜底：攻击前的模式本身也是 None（极端情况），退回走路
    RestoreMode = MOVE_Walking;
  }
  Movement->SetMovementMode(RestoreMode);

  // 恢复移动时把残留速度清掉：MOVE_None 期间速度不会被清零，
  // 带着攻击前的速度起身会「滑」一下。
  Movement->StopMovementImmediately();

  WFLOG_INFO("[攻击] %s 攻击结束：恢复移动（移动模式 %d，本端权威=%d）。",
             *GetName(), static_cast<int32>(RestoreMode), HasAuthority() ? 1 : 0);
}
