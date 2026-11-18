#pragma once

#include "GameFramework/Actor/MyCharacter.h"
#include "KurisuCharacter.generated.h"

class UKurisuAnimInstance;

// 믹사모 Kurisu 3인칭 캐릭터. 카메라·스프링 암은 AMyCharacter(UE 3인칭 템플릿)를 그대로 쓴다.
// 지상 이동은 애니메이션 루트 모션이 한다: 입력은 방향(몸 회전)과 원하는 속도(Blend Space 입력)만 정한다.
// 조작: WASD 이동(기본 Slow Run) / Shift 짧게 = 구르기, 길게 = Run / C 누르는 동안 Walk / Space 점프 / E 슬라이드
UCLASS()
class AKurisuCharacter : public AMyCharacter
{
	GENERATED_BODY()

public:
	AKurisuCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	virtual void Jump() override;
	void Roll();
	void Slide();

	// Blend Space가 읽는 원하는 지면 속도 (cm/s). 입력에 따라 SpeedChangeRate로 부드럽게 바뀐다
	float GetDesiredGroundSpeed() const { return DesiredGroundSpeed; }

	// 원하는 속도가 바뀌는 빠르기 (cm/s²). 작을수록 출발·정지가 느리고 부드럽다
	UPROPERTY(EditAnywhere)
	float SpeedChangeRate = 450.0f;
	// Shift를 이 시간(초)보다 짧게 눌렀다 떼면 구르기, 더 오래 누르고 있으면 Run
	UPROPERTY(EditAnywhere)
	float RollTapTime = 0.22f;
	// 원하는 속도가 이 값(cm/s) 이상일 때만 슬라이드한다
	UPROPERTY(EditAnywhere)
	float MinSlideSpeed = 200.0f;
	// 입력 방향으로 몸을 돌리는 속도 (deg/s)
	UPROPERTY(EditAnywhere)
	float TurnRate = 540.0f;

private:
	UKurisuAnimInstance* GetKurisuAnimInstance() const;
	float GetHorizontalSpeedCm() const;
	void TurnTowardInput(float DeltaTime);
	// 입력(가속) 방향이 있으면 액션 전에 그쪽으로 몸을 바로 돌린다 (루트 모션 액션 중에는 돌지 않는다)
	void FaceAccelerationDirection();
	void UpdateMovementSpeedLimit();

	float DesiredGroundSpeed = 0.0f;
	float ShiftHeldTime = 0.0f;
	bool bShiftWasDown = false;
	bool bWasFalling = false;
	float JumpCarrySpeed = 0.0f;   // 점프를 누른 순간의 속도 (cm/s). 준비 동작과 공중에서 이 속도를 유지한다
	float AirCarrySpeed = 0.0f;
};
