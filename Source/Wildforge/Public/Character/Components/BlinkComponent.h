// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

#include "Animation/AnimMontage.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"

#include "BlinkComponent.generated.h"

// 闪现落地的本地广播（每端各自在本地收到一次）。
//
// 与 Multicast 的区别：Multicast 只能由服务器发起，而且必须在网络上跑一趟；
// 本委托在「本端真的完成了这次位移」时立刻广播，订阅者（镜头震动 / 残影 / 后处理）
// 能零延迟响应。当前实现里它由 `Multicast_PlayBlinkEffects` 在**每个端**驱动，
// 所以两端的特效表现一致。
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBlinkPerformed,
                                             FVector, LandingLocation,
                                             bool, bSucceeded);

/**
 * 点按闪现组件 —— **服务器权威**。
 *
 * ## 权威边界一览
 *
 * | 内容 | 权威端 | 客户端 | 通道 |
 * |---|---|---|---|
 * | 位移本身 `BlinkForward()` | 服务器 | ✗ 调不动（Absorbed） | `Server_Blink` / `Server_BlinkForward` |
 * | 距离上限收敛 / 落点合法性 | 服务器 | ✗ | 在权威函数内部完成 |
 * | `MaxBlinkDistance` 等参数 | 服务器 | 只读 | 类默认值随 Actor 生成同步 |
 * | `LastBlinkTime` / `BlinkCount` | 服务器写 | 只读（复制下来） | `DOREPLIFETIME_CONDITION(COND_OwnerOnly)` |
 * | 蒙太奇（可选）/ 特效 | 服务器发起 | 跟随 | `Multicast_PlayBlinkMontage` / `Multicast_PlayBlinkEffects` |
 *
 * ## 为什么方向是「视线水平分量」而不是「输入方向」
 *
 * 点按闪现不该受 WASD 影响（玩家可能在后退时按闪现躲技能）。取视线水平分量还有两个好处：
 * 朝上不会顶到天花板、朝下不会钻进地面。
 *
 * ## 落点为什么要校验「有可站立面」
 *
 * 只做 Sweep 是不够的：贴着墙边闪一下很容易落到墙外的**悬空**位置，
 * 玩家会「闪」到半空然后掉下去，既难解释也不好用。所以位移之后还要向下探一次，
 * 没有可站立面就整体回退到起点并返回 false。
 *
 * ⚠️ `UActorComponent` 上没有 `HasAuthority()`（那是 `AActor` 的方法，直接写会 C3861），
 * 组件里一律用 `Utils/WildforgeAuthority.h` 的 `IsAuthoritativeForActorComponent(this)`。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WILDFORGE_API UBlinkComponent : public UActorComponent {
  GENERATED_BODY()

public:
  // Sets default values for this component's properties
  UBlinkComponent();

  virtual void GetLifetimeReplicatedProps(
      TArray<FLifetimeProperty> &OutLifetimeProps) const override;

  // ===== 可配置参数（类默认值，随 Actor 生成同步，不参与运行时复制）=====

  // 闪现的总开关（眩晕 / 缴械 / 死亡 / 落地硬直等状态把它关掉）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink")
  bool bCanBlink = true;

  // 闪现允许的最大距离（cm）。
  // **服务器侧的上限**：`Server_BlinkForward` 传了更大的值也只会被收敛到它；
  // `Server_Blink` 直接用它当请求距离。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "cm"))
  float MaxBlinkDistance = 1200.0f;

  // 两次闪现之间的最小间隔（秒），0 = 不限制。
  // 校验在服务器侧做，防止被篡改的客户端刷包连闪。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink",
            meta = (ClampMin = "0", UIMin = "0", ForceUnits = "s"))
  float BlinkCooldown = 0.0f;

  // 闪现后是否保留原有水平速度（true = 手感更连续；false = 落地急停，防「闪完继续滑」）
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink")
  bool bKeepVelocityAfterBlink = false;

  // 扫描用的碰撞通道。默认 `ECC_Visibility` 与引擎的视线检测一致。
  // ⚠️ 别用胶囊体自身会阻塞的通道（比如 Pawn），扫描一开始就会撞到自己。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink")
  TEnumAsByte<ECollisionChannel> BlinkTraceChannel = ECC_Visibility;

  // ===== 可选表现：闪现蒙太奇 =====
  //
  // **留空是完全合法的**：内容仓库里当前并没有闪现用的蒙太奇资源，所以默认就是空的，
  // 此时播放会被跳过并记一条 INFO（不是 WARNING，空配置是设计内的状态而不是错误）。
  //
  // 同样**不订阅** `OnMontageEnded`：闪现是「一次性位移」，生命周期由
  // `BlinkForward()` 的返回值决定，不由动画决定。
  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink|Anim")
  TObjectPtr<UAnimMontage> BlinkMontage;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Blink|Anim",
            meta = (ClampMin = "0.01", UIMin = "0.01"))
  float BlinkMontagePlayRate = 1.0f;

  // ===== 状态（服务器写、拥有者客户端只读）=====

  // 上一次闪现的世界时间（秒）。服务器用它做冷却判定；复制给客户端只为调试与 UI。
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Blink")
  float LastBlinkTime = -1000.0f;

  // 成功闪现的累计次数（调试 / 成就 / UI 用），只有校验通过的那次才 +1
  UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated,
            Category = "Blink")
  int32 BlinkCount = 0;

  // ===== 通知（每端各自本地广播）=====

  UPROPERTY(BlueprintAssignable, Category = "Blink")
  FOnBlinkPerformed OnBlinkPerformed;

  // ===== 查询（纯读，任何端都能安全调用）=====

  // 冷却是否已经走完（服务器侧判定依据；客户端读它也准，因为它基于复制的 LastBlinkTime）
  UFUNCTION(BlueprintPure, Category = "Blink", meta = (BlueprintThreadSafe))
  bool IsBlinkReady() const;

  // 剩余冷却时间（秒），0 = 可以闪
  UFUNCTION(BlueprintPure, Category = "Blink", meta = (BlueprintThreadSafe))
  float GetBlinkCooldownRemaining() const;

  // 闪现状态的一行快照，专供日志 / 调试用
  UFUNCTION(BlueprintPure, Category = "Blink", meta = (BlueprintThreadSafe))
  FString GetBlinkDebugString() const;

  // ===== 服务器权威函数（BlueprintAuthorityOnly + 内部开发期门禁）=====
  // 客户端直接调会被引擎静默丢弃（callspace = Absorbed），请改调下方 `Server_*`。

  // 沿角色视线水平方向做一次带碰撞扫描的瞬移。
  //
  // 行为：
  //   - 方向取 `GetActorEyesViewPoint` 的水平分量（忽略俯仰），所以朝上不会顶到天花板、
  //     朝下不会钻进地面；没有 Controller 时退回角色自身前向。
  //   - 先用胶囊体做整段 `SweepSingleByChannel`：撞墙 / 撞柱子就取消（不穿墙）；
  //     撞到可站立的地面则把落点贴回坡面，而不是嵌进去。
  //   - 再用 `SetActorLocation(..., bSweep=true, TeleportPhysics)` 做真正的位置校验
  //     （自己写穿透检测容易漏掉斜坡 / 台阶）。
  //   - 落点校验：高于最大可站立坡度、或**离地悬空**（会直接掉下去）的落点一律判定失败，
  //     并把角色整体退回起点。
  //   - 结束时按 `bKeepVelocity` 处理速度，防止闪现后角色被旧速度带着继续滑。
  //
  // 参数（注意：输出引用必须排在带默认值的参数之前，否则编译器会报
  // "missing default argument on parameter 'OutLandingLocation'"，这是 C++ 规则，
  // UHT 不会替你发现——见 bug-016）：
  //   OutLandingLocation 闪现落点（供特效 / 调试用）；失败时为调用前的原点位置
  //   Distance       请求的闪现距离（cm）
  //   MaxDistance    本次允许的最大距离（cm），会再与 `MaxBlinkDistance` 取小
  //   bKeepVelocity  true = 保留原有水平速度；false = 落地急停
  //   TraceChannel   扫描用的碰撞通道
  //
  // 返回是否真的完成了位移。
  //
  // ⚠️ 两个容易踩的坑（都踩过）：
  //   1. 参数类型写 `ECollisionChannel` 而不是 `TEnumAsByte<ECollisionChannel>`：
  //      `BlueprintNativeEvent` 的 `_Implementation` 是**由 UHT 生成声明**的，
  //      它对枚举参数生成的是裸 `ECollisionChannel`。头文件里写 TEnumAsByte 会让
  //      生成的声明与 .cpp 里的定义签名不一致，报
  //      `out-of-line definition ... does not match any declaration`。
  //      （旧实现能编，是因为它在 AActor 上且只是 BlueprintCallable，没有生成声明。）
  //   2. 默认值必须与 `=` **写在同一行**：把 `ECC_Visibility` 折到下一行同样会
  //      报上面的错——别为了排版好看折行。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Blink")
  bool BlinkForward(FVector &OutLandingLocation, float Distance,
                    float MaxDistance = 1200.0f, bool bKeepVelocity = false,
                    ECollisionChannel TraceChannel = ECC_Visibility);

  // 按组件配置的 `MaxBlinkDistance` 闪现（最常用的入口：方向由视线决定，
  // 距离用设计器里的值）。`OutLandingLocation` 只在需要特效落点时才有用。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly,
            Category = "Blink")
  bool BlinkToConfiguredDistance(FVector &OutLandingLocation);

  // 清除冷却（服务器侧）：复活 / 传送 / 调试用
  UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Blink")
  void ResetBlinkCooldown();

  // ===== 表现同步（服务器 -> 所有端）=====
  // 引擎默认**不复制蒙太奇播放**，所以想让客户端也看到闪现动画必须显式 Multicast。
  // 参数只带「落点」与「是否成功」——客户端不读任何权威状态来决定演什么。
  //
  // ⚠️ 两个 `Multicast_*` 的 `_Implementation` **不能加「非权威端就忽略」的门禁**：
  //    客户端执行的那一次就是「收到服务器的表现同步」，门禁会让客户端永远看不到表现
  //    （引擎规则：Multicast 在客户端 callspace = Local 且不再转发，`Actor.cpp:5500-5519`）。

  UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Blink|RPC")
  void Multicast_PlayBlinkMontage(FVector InLandingLocation);

  // 在各端播放闪现表现，并在每端广播 `OnBlinkPerformed`。
  // 蓝图覆写 `PlayBlinkEffects` 即可实现具体效果（残影 / 音效 / 镜头抖动）。
  UFUNCTION(NetMulticast, Reliable, BlueprintCallable, Category = "Blink|RPC")
  void Multicast_PlayBlinkEffects(FVector InLandingLocation, bool bSucceeded);

  // 蓝图覆写的表现入口（纯表现，任何端都可以本地调）。C++ 默认什么都不做。
  UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Blink")
  void PlayBlinkEffects(FVector InLandingLocation, bool bSucceeded);

  // ===== 客户端 -> 服务器：闪现请求 =====
  // 输入绑定通常写在蓝图里（模板的 Enhanced Input 的 IA_Shift_Dash）：
  // 判定「点按」后发 `Server_Blink`。C++ 侧不重复绑定输入，避免与蓝图绑定叠加。
  // `_Validate` 只做廉价参数检查；真正的距离收敛与落点校验在权威函数内部完成。

  // 按服务器配置的距离闪现，客户端不需要自己抬参数
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Blink|RPC")
  void Server_Blink();

  // 请求指定距离的闪现：距离由客户端提出、服务器收敛到 `MaxBlinkDistance` 以内
  UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable,
            Category = "Blink|RPC")
  void Server_BlinkForward(float Distance, float MaxDistance = 1200.0f,
                           bool bKeepVelocity = false);
};
