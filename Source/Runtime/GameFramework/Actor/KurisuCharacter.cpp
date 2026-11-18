#include "EnginePCH.h"
#include "GameFramework/Actor/KurisuCharacter.h"
#include "GameFramework/Animation/KurisuAnimInstance.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Input/KeyCode.h"
#include <algorithm>
#include <cmath>

AKurisuCharacter::AKurisuCharacter()
{
	// Kurisu 메시는 cm 단위라 0.01배. 원점이 발이라 캡슐 바닥까지 내린다. 클립 진행 방향이 +X라 Yaw 0
	CapsuleComponent->CapsuleHalfHeight = 0.9f;
	CapsuleComponent->CapsuleRadius = 0.35f;
	SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -0.9f), FRotator(0.0f, 0.0f, 0.0f));
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	// 3인칭 카메라: 어깨 높이에서 3.5m 뒤. 약간의 위치 지연으로 루트 모션의 걸음 흔들림이 카메라에 덜 전해진다
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.5f));
	SpringArmComponent->TargetArmLength = 3.5f;
	SpringArmComponent->bEnableCameraLag = true;
	SpringArmComponent->CameraLagSpeed = 10.0f;

	// 지상 이동은 루트 모션이 한다. 루트 모션이 없는 프레임(Idle, 점프 준비)에 입력으로 미끄러지지 않게 속도 한도를 0으로 두고,
	// 필요한 때만 UpdateMovementSpeedLimit가 연다. 몸 회전도 직접 한다 (루트 모션 중에는 PhysicsRotation이 돌지 않으므로)
	CharacterMovementComponent->MaxWalkSpeed = 0.0f;
	CharacterMovementComponent->bOrientRotationToMovement = false;
	CharacterMovementComponent->BrakingDecelerationWalking = 1200.0f;
}

void AKurisuCharacter::BeginPlay()
{
	// AMyCharacter::BeginPlay는 UMyAnimInstance를 끼우므로 건너뛰고, AnimBP가 없을 때만 Kurisu 전용 인스턴스를 쓴다
	ACharacter::BeginPlay();

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !GetKurisuAnimInstance())
	{
		SkeletalMeshComponent->SetAnimInstance(NewObject<UKurisuAnimInstance>(SkeletalMeshComponent));
	}
}

void AKurisuCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	const APlayerController* PC = GetController();
	const bool bShiftDown = PC && PC->IsKeyDown(static_cast<int32>(EKeyCode::Shift));
	const bool bWalkDown = PC && PC->IsKeyDown(static_cast<int32>(EKeyCode::C));

	// Shift: 짧게 눌렀다 떼면 구르기, 계속 누르고 있으면 Run
	if (bShiftDown)
	{
		ShiftHeldTime += DeltaTime;
	}
	else if (bShiftWasDown)
	{
		if (ShiftHeldTime < RollTapTime)
			Roll();
		ShiftHeldTime = 0.0f;
	}
	bShiftWasDown = bShiftDown;
	const bool bRunning = bShiftDown && ShiftHeldTime >= RollTapTime;

	// 원하는 속도: 입력이 있으면 고른 단계의 클립 속도, 없으면 0. Blend Space가 이 값으로 클립을 섞는다
	const FVector Input = CharacterMovementComponent->GetCurrentAcceleration();
	const bool bHasInput = Input.X * Input.X + Input.Y * Input.Y > 1.0e-6f;
	float TargetSpeed = 0.0f;
	if (bHasInput && Anim)
	{
		TargetSpeed = bRunning ? Anim->GetRunSpeed() : (bWalkDown ? Anim->GetWalkSpeed() : Anim->GetSlowRunSpeed());
	}
	const float MaxStep = SpeedChangeRate * DeltaTime;
	DesiredGroundSpeed += std::clamp(TargetSpeed - DesiredGroundSpeed, -MaxStep, MaxStep);

	// 액션(구르기·슬라이드·점프 후반)이 아닐 때 입력 방향으로 돈다
	if (!IsFalling() && bHasInput && (!Anim || !Anim->IsInAction() || Anim->IsInJumpWindup()))
	{
		TurnTowardInput(DeltaTime);
	}

	// 클립의 도약 프레임에 맞춰 실제로 뜬다. 체공 시간이 클립과 같도록 점프 속도를 정한다 (v = g * t / 2)
	if (Anim && Anim->ConsumeJumpTakeoff() && CanJump())
	{
		const float Gravity = CharacterMovementComponent->Gravity * CharacterMovementComponent->GravityScale;
		CharacterMovementComponent->JumpZVelocity = Gravity * Anim->GetJumpAirTime() * 0.5f * WorldToCm;
		AirCarrySpeed = JumpCarrySpeed;
		CharacterMovementComponent->DoJump();
	}

	UpdateMovementSpeedLimit();
}

void AKurisuCharacter::UpdateMovementSpeedLimit()
{
	const UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	const bool bFalling = IsFalling();
	if (bFalling && !bWasFalling && AirCarrySpeed <= 0.0f)
	{
		// 점프가 아니라 발판에서 떨어진 경우: 떨어진 순간의 속도를 유지한다
		AirCarrySpeed = GetHorizontalSpeedCm();
	}
	if (!bFalling && bWasFalling)
	{
		AirCarrySpeed = 0.0f;
	}
	bWasFalling = bFalling;

	// 루트 모션이 없는 구간에서만 입력 이동이 쓰인다. 공중과 점프 준비 동작에서는 누른 순간의 속도를 이어 가고, 나머지는 0
	if (bFalling)
		CharacterMovementComponent->MaxWalkSpeed = AirCarrySpeed;
	else if (Anim && Anim->IsInJumpWindup())
		CharacterMovementComponent->MaxWalkSpeed = JumpCarrySpeed;
	else
		CharacterMovementComponent->MaxWalkSpeed = 0.0f;
}

void AKurisuCharacter::Jump()
{
	// 바로 뜨지 않는다. 클립이 웅크렸다가 도약 프레임에 닿으면 Tick에서 뜬다
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || !CanJump() || Anim->IsInAction())
		return;

	JumpCarrySpeed = GetHorizontalSpeedCm();
	Anim->RequestJump();
}

void AKurisuCharacter::Roll()
{
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || IsFalling() || Anim->IsInAction())
		return;

	FaceAccelerationDirection();
	Anim->RequestRoll();
}

void AKurisuCharacter::Slide()
{
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || IsFalling() || Anim->IsInAction() || DesiredGroundSpeed < MinSlideSpeed)
		return;

	FaceAccelerationDirection();
	Anim->RequestSlide();
}

UKurisuAnimInstance* AKurisuCharacter::GetKurisuAnimInstance() const
{
	return Cast<UKurisuAnimInstance>(SkeletalMeshComponent->GetAnimInstance());
}

float AKurisuCharacter::GetHorizontalSpeedCm() const
{
	const FVector Velocity = GetVelocity();
	return FVector(Velocity.X, Velocity.Y, 0.0f).Size() * WorldToCm;
}

void AKurisuCharacter::TurnTowardInput(float DeltaTime)
{
	const FVector Input = CharacterMovementComponent->GetCurrentAcceleration();
	if (!GetRootComponent())
		return;

	FRotator Rotation = GetActorRotation();
	const float DesiredYaw = std::atan2(Input.Y, Input.X) * (180.0f / 3.14159265f);
	const float Delta = FMath::UnwindDegrees(DesiredYaw - Rotation.Yaw);
	const float MaxTurn = TurnRate * DeltaTime;
	Rotation.Yaw = std::abs(Delta) <= MaxTurn ? DesiredYaw : FMath::NormalizeAxis(Rotation.Yaw + (Delta > 0.0f ? MaxTurn : -MaxTurn));
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void AKurisuCharacter::FaceAccelerationDirection()
{
	const FVector Input = CharacterMovementComponent->GetCurrentAcceleration();
	if (Input.X * Input.X + Input.Y * Input.Y < 1.0e-6f || !GetRootComponent())
		return;

	FRotator Rotation = GetActorRotation();
	Rotation.Yaw = std::atan2(Input.Y, Input.X) * (180.0f / 3.14159265f);
	GetRootComponent()->SetRelativeRotation(Rotation);
}
