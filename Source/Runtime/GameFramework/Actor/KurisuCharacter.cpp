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

namespace
{
	constexpr float RadToDeg = 180.0f / 3.14159265f;

	bool IsNonZero2D(const FVector& V)
	{
		return V.X * V.X + V.Y * V.Y > 1.0e-6f;
	}
}

AKurisuCharacter::AKurisuCharacter()
{
	// Kurisu 메시는 cm 단위라 0.01배. 원점이 발이라 캡슐 바닥까지 내린다. 클립 진행 방향이 +X라 Yaw 0
	CapsuleComponent->CapsuleHalfHeight = 0.9f;
	CapsuleComponent->CapsuleRadius = 0.35f;
	SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -0.9f), FRotator(0.0f, 0.0f, 0.0f));
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	// 3인칭 카메라: 어깨 높이에서 3.5m 뒤. 약간의 위치 지연으로 급정지·급회전이 화면에서 덜 튄다
	SpringArmComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 0.5f));
	SpringArmComponent->TargetArmLength = 3.5f;
	SpringArmComponent->bEnableCameraLag = true;
	SpringArmComponent->CameraLagSpeed = 14.0f;

	ApplyMovementSettings();
}

void AKurisuCharacter::ApplyMovementSettings()
{
	// 속도 한도는 Tick이 상태마다 정한다. 몸 회전도 직접 한다 (이동 방향이 아니라 입력 방향을 본다)
	CharacterMovementComponent->MaxWalkSpeed = 0.0f;
	CharacterMovementComponent->MaxAcceleration = GroundAcceleration;
	CharacterMovementComponent->BrakingDecelerationWalking = GroundBraking;
	CharacterMovementComponent->GroundFriction = GroundFriction;
	CharacterMovementComponent->AirControl = AirControl;
	CharacterMovementComponent->GravityScale = JumpGravityScale;
	CharacterMovementComponent->bOrientRotationToMovement = false;
}

void AKurisuCharacter::BeginPlay()
{
	// AMyCharacter::BeginPlay는 UMyAnimInstance를 끼우므로 건너뛰고, AnimBP가 없을 때만 Kurisu 전용 인스턴스를 쓴다
	ACharacter::BeginPlay();

	// 씬에 저장된 이동 컴포넌트 값(예전 루트 모션 설정)이 생성자 값을 덮으므로 여기서 다시 맞춘다
	ApplyMovementSettings();

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !GetKurisuAnimInstance())
	{
		SkeletalMeshComponent->SetAnimInstance(NewObject<UKurisuAnimInstance>(SkeletalMeshComponent));
	}
}

void AKurisuCharacter::AddMovementInput(const FVector& WorldDirection, float ScaleValue)
{
	// 방향은 늘 기억한다 (액션 캔슬, 구르기 방향, 회전에 쓴다). 이동으로는 받아도 될 때만 넘긴다
	PendingInput += WorldDirection * ScaleValue;
	if (AcceptsMoveInput())
	{
		Super::AddMovementInput(WorldDirection, ScaleValue);
	}
}

bool AKurisuCharacter::AcceptsMoveInput() const
{
	const UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	return !Anim || IsFalling() || !Anim->IsInAction() || Anim->IsInJumpWindup();
}

void AKurisuCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 컨트롤러가 이 Tick 앞에 돌든 뒤에 돌든, 가장 최근 한 프레임 입력을 쓴다
	LastInput = PendingInput;
	PendingInput = FVector::ZeroVector;

	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	const APlayerController* PC = GetController();
	const bool bRunning = PC && PC->IsKeyDown(static_cast<int32>(EKeyCode::Shift));
	const bool bHasInput = HasMovementInput();
	const bool bFalling = IsFalling();
	const float Speed = GetHorizontalSpeedCm();
	UCharacterMovementComponent* Movement = CharacterMovementComponent;

	// 공중 진입·착지 시 들고 갈 속도 정리
	if (bFalling && !bWasFalling && AirCarrySpeed <= 0.0f)
	{
		AirCarrySpeed = Speed;   // 점프가 아니라 발판에서 떨어진 경우: 떨어진 순간의 속도
	}
	if (!bFalling && bWasFalling)
	{
		AirCarrySpeed = 0.0f;
	}
	bWasFalling = bFalling;

	const float WalkSpeed = Anim ? Anim->GetWalkSpeed() : 150.0f;
	const bool bDashing = Anim && Anim->IsInDashState();
	if (bDashing && !bWasDashing)
	{
		DashCarrySpeed = Speed;
	}
	bWasDashing = bDashing;

	if (bDashing)
	{
		// 구르기·슬라이드: 클립이 지금 움직이는 속도로 몸 앞 방향으로 간다. 입력으로는 살짝만 튼다
		if (bHasInput)
			TurnTowardInput(DeltaTime, DashSteerRate);

		// 클립 앞부분(준비 동작)은 거의 안 움직이므로, 달리던 속도를 감속하며 이어 가 멈칫하지 않게 한다
		DashCarrySpeed = std::max(DashCarrySpeed - GroundBraking * DeltaTime, 0.0f);
		const float DashSpeed = std::max(Anim->GetDashTravelSpeed(), DashCarrySpeed);
		const FVector Forward = GetActorRotation().Quaternion().GetForwardVector();
		const FVector Flat = FVector(Forward.X, Forward.Y, 0.0f).GetSafeNormal();
		Movement->MaxWalkSpeed = DashSpeed;
		Movement->Velocity = FVector(Flat.X * DashSpeed * CmToWorld, Flat.Y * DashSpeed * CmToWorld, Movement->Velocity.Z);
		Super::AddMovementInput(Flat, 1.0f);   // 감속이 걸리지 않게 앞으로 가속도 준다
	}
	else if (bFalling)
	{
		// 공중: 뛰어오른 속도를 유지하되, 입력이 있으면 AirMoveSpeed까지는 밀고 나갈 수 있다
		Movement->MaxWalkSpeed = std::max({ AirCarrySpeed, WalkSpeed, bHasInput ? AirMoveSpeed : 0.0f });
		if (bHasInput)
			TurnTowardInput(DeltaTime, AirTurnRate);
	}
	else if (Anim && Anim->IsInJumpWindup())
	{
		Movement->MaxWalkSpeed = std::max(JumpCarrySpeed, WalkSpeed);
		if (bHasInput)
			TurnTowardInput(DeltaTime, TurnRate);
	}
	else if (Anim && Anim->IsInAction())
	{
		// 공격: 그 자리에 선다 (입력은 막혀 있으므로 감속으로 바로 멈춘다)
		Movement->MaxWalkSpeed = 0.0f;
	}
	else
	{
		// Locomotion: Shift면 Run(클립보다 빠른 구간은 재생 속도를 올려 맞춘다), 아니면 Walk
		Movement->MaxWalkSpeed = (bRunning && Anim) ? Anim->GetMaxRunSpeed() : WalkSpeed;
		if (bHasInput)
			TurnTowardInput(DeltaTime, Speed < 60.0f ? StandingTurnRate : TurnRate);
	}

	// 중력: 올라갈 때 기본, Space를 떼면 세게(낮은 점프), 내려올 때 더 세게
	const float BaseGravity = Movement->Gravity * JumpGravityScale;   // m/s²
	const float VerticalSpeed = Movement->Velocity.Z;                  // m/s
	const bool bJumpHeld = PC && PC->IsKeyDown(static_cast<int32>(EKeyCode::Space));
	float GravityMultiplier = 1.0f;
	if (bFalling && VerticalSpeed <= 0.0f)
		GravityMultiplier = FallGravityMultiplier;
	else if (bFalling && bJumpRising && bJumpCutArmed && !bJumpHeld)
		GravityMultiplier = JumpCutGravityMultiplier;
	Movement->GravityScale = JumpGravityScale * GravityMultiplier;

	// 정점: 실제로 올라간 높이로 착지 속도를 계산해 클립의 낙하 구간을 맞춘다
	if (bJumpRising && bFalling && VerticalSpeed <= 0.0f)
	{
		bJumpRising = false;
		const float Rise = std::max(GetActorLocation().Z - TakeoffZ, 0.01f);
		if (Anim)
			Anim->SetJumpLandSpeed(std::sqrt(2.0f * BaseGravity * FallGravityMultiplier * Rise) * WorldToCm);
	}
	if (!bFalling)
		bJumpRising = false;

	// 클립의 도약 프레임에 맞춰 실제로 뜬다. JumpHeight까지 오르는 속도: v = √(2gh)
	if (Anim && Anim->ConsumeJumpTakeoff() && CanJump())
	{
		const float TakeoffSpeed = std::sqrt(2.0f * BaseGravity * std::max(JumpHeight, 0.05f)) * WorldToCm;   // cm/s
		Movement->JumpZVelocity = TakeoffSpeed;
		Movement->GravityScale = JumpGravityScale;
		AirCarrySpeed = std::max(JumpCarrySpeed, Speed);

		// 앞으로 뛰려는 점프는 뜨는 순간 입력 방향으로 속도를 싣는다 (공중에서 가속을 기다리지 않게)
		const FVector Input = GetInputDirection();
		if (IsNonZero2D(Input))
		{
			const FVector Dir = FVector(Input.X, Input.Y, 0.0f).GetSafeNormal();
			const float LaunchSpeed = std::max(Speed, JumpTakeoffBoostSpeed);
			Movement->Velocity = FVector(Dir.X * LaunchSpeed * CmToWorld, Dir.Y * LaunchSpeed * CmToWorld, Movement->Velocity.Z);
			AirCarrySpeed = std::max(AirCarrySpeed, LaunchSpeed);
			FaceInputDirection();
		}
		TakeoffZ = GetActorLocation().Z;
		bJumpRising = true;
		bJumpCutArmed = bJumpHeld;
		Anim->SetJumpTakeoffSpeed(TakeoffSpeed);
		// 정점을 지나기 전 예상 착지 속도 (끝까지 누른 경우). 정점에서 실제 높이로 다시 정한다
		Anim->SetJumpLandSpeed(std::sqrt(2.0f * BaseGravity * FallGravityMultiplier * std::max(JumpHeight, 0.05f)) * WorldToCm);
		Movement->DoJump();
	}
}

void AKurisuCharacter::Jump()
{
	// 바로 뜨지 않는다. 클립이 잠깐 웅크렸다가 도약 프레임에 닿으면 Tick에서 뜬다
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || !CanJump() || Anim->IsInAction())
		return;

	JumpCarrySpeed = GetHorizontalSpeedCm();
	Anim->RequestJump();
}

void AKurisuCharacter::Roll()
{
	// 공격 후딜(손을 거둬들이는 구간)에서는 구르기로 끊을 수 있다
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || IsFalling() || !Anim->CanRoll())
		return;

	FaceInputDirection();
	Anim->RequestRoll();
}

void AKurisuCharacter::Slide()
{
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || IsFalling() || Anim->IsInAction() || GetHorizontalSpeedCm() < MinSlideSpeed)
		return;

	FaceInputDirection();
	Anim->RequestSlide();
}

void AKurisuCharacter::Jab()
{
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || IsFalling())
		return;

	// 처음 치는 순간에만 입력 방향으로 돈다. 콤보 도중에 돌면 섞이는 포즈가 튄다
	const bool bFirstHit = !Anim->IsInAction();
	if (Anim->RequestJab() && bFirstHit)
		FaceInputDirection();
}

void AKurisuCharacter::Punch()
{
	UKurisuAnimInstance* Anim = GetKurisuAnimInstance();
	if (!Anim || IsFalling())
		return;

	const bool bFirstHit = !Anim->IsInAction();
	if (Anim->RequestPunch() && bFirstHit)
		FaceInputDirection();
}

FVector AKurisuCharacter::GetInputDirection() const
{
	return IsNonZero2D(PendingInput) ? PendingInput : LastInput;
}

bool AKurisuCharacter::HasMovementInput() const
{
	return IsNonZero2D(GetInputDirection());
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

void AKurisuCharacter::TurnTowardInput(float DeltaTime, float Rate)
{
	const FVector Input = GetInputDirection();
	if (!GetRootComponent() || !IsNonZero2D(Input))
		return;

	FRotator Rotation = GetActorRotation();
	const float DesiredYaw = std::atan2(Input.Y, Input.X) * RadToDeg;
	const float Delta = FMath::UnwindDegrees(DesiredYaw - Rotation.Yaw);
	const float MaxTurn = Rate * DeltaTime;
	Rotation.Yaw = std::abs(Delta) <= MaxTurn ? DesiredYaw : FMath::NormalizeAxis(Rotation.Yaw + (Delta > 0.0f ? MaxTurn : -MaxTurn));
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

void AKurisuCharacter::FaceInputDirection()
{
	const FVector Input = GetInputDirection();
	if (!IsNonZero2D(Input) || !GetRootComponent())
		return;

	FRotator Rotation = GetActorRotation();
	Rotation.Yaw = std::atan2(Input.Y, Input.X) * RadToDeg;
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(Rotation);
}
