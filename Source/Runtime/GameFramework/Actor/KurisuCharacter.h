#pragma once

#include "GameFramework/Actor/MyCharacter.h"
#include "KurisuCharacter.generated.h"

class UKurisuAnimInstance;

// 믹사모 Kurisu 3인칭 캐릭터. 카메라·스프링 암은 AMyCharacter(UE 3인칭 템플릿)를 그대로 쓴다.
// 이동은 전부 코드(CharacterMovement)가 하고, 애니메이션은 실제 속도를 따라간다 (루트 모션 없음).
// 구르기·슬라이드는 클립이 그 순간 움직이는 속도를 읽어 몸 앞 방향 속도로 넣는다 (모양은 클립 그대로, 조향·캔슬은 코드).
// 조작: WASD 이동(기본 Walk) / Shift 누르는 동안 Run / R 구르기 / Space 점프 / E 슬라이드 / 좌클릭 잽 / 우클릭 펀치
UCLASS()
class AKurisuCharacter : public AMyCharacter
{
	GENERATED_BODY()

public:
	AKurisuCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	// 플레이어 입력은 여기서 가로챈다. 액션(구르기·슬라이드·공격) 중에는 이동으로 넘기지 않고 방향만 기억한다
	virtual void AddMovementInput(const FVector& WorldDirection, float ScaleValue = 1.0f) override;

	virtual void Jump() override;
	void Roll();
	void Slide();
	// 좌클릭 잽(왼손) / 우클릭 펀치(오른손). 공격 도중에 반대 손을 누르면 콤보로 이어진다
	void Jab();
	void Punch();

	// 지금 수평 속도 (cm/s). Blend Space가 이 값으로 클립을 고른다
	float GetGroundSpeed() const { return GetHorizontalSpeedCm(); }
	// WASD 같은 이동 입력이 들어와 있나 (액션 중에 막힌 입력도 포함)
	bool HasMovementInput() const;

	// 지상 가속 / 손을 뗐을 때 감속 (cm/s²). 클수록 출발·정지가 즉각적이다
	UPROPERTY(EditAnywhere)
	float GroundAcceleration = 2800.0f;
	UPROPERTY(EditAnywhere)
	float GroundBraking = 2600.0f;
	// 방향 전환 시 옆으로 미끄러지는 속도 성분을 깎는 정도 (클수록 바로 꺾인다)
	UPROPERTY(EditAnywhere)
	float GroundFriction = 12.0f;
	// 공중에서 입력이 먹는 정도 (0~1)
	UPROPERTY(EditAnywhere)
	float AirControl = 0.85f;
	// 공중에서 입력으로 낼 수 있는 최소 속도 상한 (cm/s). 걷다가 뛰어도 이 속도까지는 앞으로 나간다
	UPROPERTY(EditAnywhere)
	float AirMoveSpeed = 420.0f;
	// 뜨는 순간 이동 입력이 있으면 그 방향으로 최소 이 속도(cm/s)를 바로 싣는다
	UPROPERTY(EditAnywhere)
	float JumpTakeoffBoostSpeed = 360.0f;
	// 이 속도(cm/s) 이상일 때만 슬라이드한다
	UPROPERTY(EditAnywhere)
	float MinSlideSpeed = 200.0f;
	// 입력 방향으로 몸을 돌리는 속도 (deg/s). 거의 서 있을 때는 StandingTurnRate로 바로 돈다
	UPROPERTY(EditAnywhere)
	float TurnRate = 900.0f;
	UPROPERTY(EditAnywhere)
	float StandingTurnRate = 1800.0f;
	// 구르기·슬라이드 중 입력 방향으로 살짝 트는 속도 (deg/s)
	UPROPERTY(EditAnywhere)
	float DashSteerRate = 150.0f;
	UPROPERTY(EditAnywhere)
	float AirTurnRate = 360.0f;

	// 점프: Space를 끝까지 누르면 이 높이(m)까지 뜬다
	UPROPERTY(EditAnywhere)
	float JumpHeight = 1.3f;
	// 점프·낙하 중력 배율. 기본 중력(9.81)은 사람 크기 캐릭터에는 둥둥 떠 보인다
	UPROPERTY(EditAnywhere)
	float JumpGravityScale = 2.6f;
	// 내려올 때 중력을 이만큼 더 세게 (올라갈 때보다 빨리 떨어져 묵직하다)
	UPROPERTY(EditAnywhere)
	float FallGravityMultiplier = 1.5f;
	// 가변 점프: 도약 순간 Space를 누르고 있다가 올라가는 중에 떼면 중력을 이만큼 세게 해 낮게 뛴다. 1이면 끔.
	// 클립이 웅크렸다가 뜨므로 Space를 톡 치면 도약 전에 이미 손이 떨어진다 → 그 경우는 늘 끝까지 뛴다
	UPROPERTY(EditAnywhere)
	float JumpCutGravityMultiplier = 1.0f;

private:
	UKurisuAnimInstance* GetKurisuAnimInstance() const;
	float GetHorizontalSpeedCm() const;
	FVector GetInputDirection() const;
	void TurnTowardInput(float DeltaTime, float Rate);
	// 입력 방향이 있으면 액션 전에 그쪽으로 몸을 바로 돌린다
	void FaceInputDirection();
	void ApplyMovementSettings();
	// 지금 플레이어 입력을 이동으로 넘겨도 되나 (지상 Locomotion / 점프 준비 / 공중)
	bool AcceptsMoveInput() const;

	FVector PendingInput = FVector::ZeroVector;   // 이번 프레임에 들어온 플레이어 입력 (막힌 것 포함)
	FVector LastInput = FVector::ZeroVector;      // 지난 Tick까지 들어온 입력
	bool bWasFalling = false;
	float JumpCarrySpeed = 0.0f;   // 점프를 누른 순간의 속도 (cm/s)
	float AirCarrySpeed = 0.0f;
	bool bJumpRising = false;      // 점프로 올라가는 중 (정점에서 끈다)
	bool bJumpCutArmed = false;    // 도약 순간 Space를 누르고 있었다 (이때만 가변 점프가 걸린다)
	float TakeoffZ = 0.0f;         // 도약한 높이 (m). 정점 높이로 낙하 속도를 잰다
	bool bWasDashing = false;
	float DashCarrySpeed = 0.0f;   // 구르기·슬라이드에 들어온 순간의 속도. 감속하며 클립 속도의 바닥값이 된다
};
