#include "EnginePCH.h"
#include "GameFramework/Actor/SonicCharacter.h"
#include "GameFramework/Animation/SonicAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Particles/ParticleEmitterInstance.h"
#include <algorithm>
#include <cmath>

namespace
{
	constexpr float RadToDeg = 180.0f / 3.14159265f;

	FVector Horizontal(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.0f);
	}
}

ASonicCharacter::ASonicCharacter()
{
	// 믹사모 메시는 cm 단위라 0.01배. 원점이 발이라 캡슐 바닥까지 내린다
	CapsuleComponent->CapsuleHalfHeight = 0.9f;
	CapsuleComponent->CapsuleRadius = 0.35f;
	SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -0.9f), FRotator(0.0f, 0.0f, 0.0f));
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	ApplyMovementSettings();

	// 코인은 머리 위쯤에서 튀어 오른다. 맞을 때만 켜므로 자동 활성화는 끈다
	CoinParticle = CreateDefaultSubobject<UParticleSystemComponent>("CoinParticle");
	CoinParticle->SetupAttachment(CapsuleComponent);
	CoinParticle->SetRelativeLocation(FVector(0.0f, 0.0f, 0.8f));
	CoinParticle->SetAutoActivate(false);

	if (FPaths::HasProject())
	{
		if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>("FBX/Sonic_SadIdle/Sonic_SadIdle.fbx.bin"))
		{
			SkeletalMeshComponent->SetSkeletalMesh(Mesh);
		}
		EnsureCoinTemplate();
	}
}

void ASonicCharacter::BeginPlay()
{
	// AMyCharacter::BeginPlay는 UMyAnimInstance를 끼우므로 건너뛰고, 소닉 클립 경로를 채운 인스턴스를 직접 만든다
	ACharacter::BeginPlay();

	EnsureCoinTemplate();
	HomeLocation = GetActorLocation();
	// 씬에 저장된 이동 컴포넌트 값(예전 샌드백 설정)이 생성자 값을 덮으므로 여기서 다시 맞춘다
	ApplyMovementSettings();

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		// 그래프는 초기화(SetAnimInstance) 시점에 경로를 읽으므로 경로를 먼저 채운다
		USonicAnimInstance* Instance = NewObject<USonicAnimInstance>(SkeletalMeshComponent);
		Instance->IdleAnimationPath = IdleAnimationPath;
		Instance->RunAnimationPath = RunAnimationPath;
		Instance->HitAnimationPath = HitAnimationPath;
		SkeletalMeshComponent->SetAnimInstance(Instance);
	}
}

void ASonicCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	USonicAnimInstance* Anim = GetSonicAnimInstance();
	if (!Anim)
		return;

	// 맞는 중에는 제자리. 피격 모션이 끝나 Run 상태에 들어간 뒤부터 도망 시간을 센다
	if (Anim->IsRunning() && FleeTimeLeft > 0.0f)
	{
		const float TargetSpeed = Anim->GetRunSpeed() * std::max(FleeSpeedScale, 0.0f);
		CharacterMovementComponent->MaxWalkSpeed = TargetSpeed;

		// 기둥·벽에 막혀 속도가 안 나면 옆으로 꺾는다
		const float Speed = Horizontal(GetVelocity()).Size() * WorldToCm;
		StuckTime = (Speed < TargetSpeed * 0.25f) ? StuckTime + DeltaTime : 0.0f;
		if (StuckTime > 0.25f)
		{
			const float Side = CoinRandom.FRandRange(0.0f, 1.0f) < 0.5f ? 1.0f : -1.0f;
			FleeDirection = FVector(-FleeDirection.Y * Side, FleeDirection.X * Side, 0.0f);
			StuckTime = 0.0f;
		}

		FleeDirection = GetLeashedFleeDirection();
		AddMovementInput(FleeDirection, 1.0f);
		TurnTowardYaw(std::atan2(FleeDirection.Y, FleeDirection.X) * RadToDeg, FleeTurnRate * DeltaTime);

		FleeTimeLeft -= DeltaTime;
		if (FleeTimeLeft <= 0.0f)
		{
			Anim->SetWantsToRun(false);
		}
		return;
	}

	CharacterMovementComponent->MaxWalkSpeed = 0.0f;
	StuckTime = 0.0f;

	// 멈춰 선 뒤에는 때린 쪽을 천천히 돌아본다
	if (!Anim->IsInHit() && bHasAttacker && LookBackTurnRate > 0.0f)
	{
		const FVector ToAttacker = Horizontal(LastAttackerLocation - GetActorLocation());
		if (ToAttacker.SizeSquared() > 1.0e-4f)
		{
			TurnTowardYaw(std::atan2(ToAttacker.Y, ToAttacker.X) * RadToDeg, LookBackTurnRate * DeltaTime);
		}
	}
}

void ASonicCharacter::SetIsHit(AActor* Attacker)
{
	USonicAnimInstance* Anim = GetSonicAnimInstance();
	if (!Anim)
	{
		// AnimBlueprint/Lua 그래프를 쓰면 기존 IsHit 변수로 처리한다
		Super::SetIsHit(Attacker);
		SpawnCoins();
		return;
	}

	// 때린 쪽 반대가 도망 방향. 때린 쪽을 모르면 지금 바라보는 반대로 달아난다
	FVector Away = -GetActorRotation().Quaternion().GetForwardVector();
	if (Attacker)
	{
		LastAttackerLocation = Attacker->GetActorLocation();
		bHasAttacker = true;
		const FVector FromAttacker = Horizontal(GetActorLocation() - LastAttackerLocation);
		if (FromAttacker.SizeSquared() > 1.0e-4f)
		{
			Away = FromAttacker;
			// 맞는 모션은 때린 쪽을 보고 재생한다
			TurnTowardYaw(std::atan2(-FromAttacker.Y, -FromAttacker.X) * RadToDeg, 360.0f);
		}
	}
	FleeDirection = Horizontal(Away).GetSafeNormal(SMALL_NUMBER, FVector(1.0f, 0.0f, 0.0f));
	FleeTimeLeft = FleeDuration;
	StuckTime = 0.0f;

	// 맞는 동안에는 그 자리에 선다 (달리던 속도는 감속으로 바로 죽는다)
	CharacterMovementComponent->MaxWalkSpeed = 0.0f;
	Anim->RequestHit();
	Anim->SetWantsToRun(true);

	SpawnCoins();
}

void ASonicCharacter::ApplyMovementSettings()
{
	// 평소에는 서 있고(속도 한도 0), 도망갈 때만 Tick이 속도 한도를 연다. 맞으면 바로 서도록 가감속을 세게 준다.
	// 몸은 Tick에서 직접 돌린다 (때린 쪽 보기 → 도망 방향 → 멈춘 뒤 돌아보기)
	CharacterMovementComponent->MaxWalkSpeed = 0.0f;
	CharacterMovementComponent->MaxAcceleration = 4000.0f;
	CharacterMovementComponent->BrakingDecelerationWalking = 4000.0f;
	CharacterMovementComponent->bOrientRotationToMovement = false;
}

void ASonicCharacter::SpawnCoins()
{
	if (!CoinParticle || !CoinParticle->GetTemplate())
		return;

	// 처음 맞을 때는 시스템을 켠다 (템플릿의 Burst가 터진다). 자동 활성화를 껐으므로 Tick도 여기서 켜진다
	if (!CoinParticle->IsActive() || CoinParticle->GetEmitterInstances().IsEmpty())
	{
		CoinParticle->Activate(true);
		return;
	}

	// 이미 코인이 떠 있으면 리셋하지 않고 지금 위치에서 한 번 더 뿌린다. 앞서 떨어진 코인은 그대로 남는다
	const FVector SpawnLocation = CoinParticle->GetWorldLocation();
	const int32 Count = CoinRandom.RandRange(std::min(CoinBurstMin, CoinBurstMax), std::max(CoinBurstMin, CoinBurstMax));
	// 코인 에미터(0번)에만 뿌린다. 다른 에미터(바닥에 닿을 때 나오는 반짝이)는 충돌 이벤트로만 생긴다
	const TArray<FParticleEmitterInstance*>& Emitters = CoinParticle->GetEmitterInstances();
	if (!Emitters.IsEmpty() && Emitters[0])
	{
		Emitters[0]->ForceSpawn(Count, SpawnLocation, FVector::ZeroVector);
	}
}

FVector ASonicCharacter::GetLeashedFleeDirection() const
{
	FVector Direction = FleeDirection;
	const FVector Offset = Horizontal(GetActorLocation() - HomeLocation);
	const float Distance = Offset.Size();
	if (LeashRadius > 0.0f && Distance > LeashRadius)
	{
		// 바깥으로 향하는 성분을 빼서 울타리를 따라 돈다. 거의 정면으로 바깥을 향하면 옆 방향을 쓴다
		const FVector Outward = Offset / Distance;
		const float OutDot = Direction.Dot(Outward);
		if (OutDot > 0.0f)
		{
			Direction = Direction - Outward * OutDot;
			if (Direction.SizeSquared() < 0.01f)
				Direction = FVector(-Outward.Y, Outward.X, 0.0f);
		}
	}
	return Horizontal(Direction).GetSafeNormal(SMALL_NUMBER, FleeDirection);
}

void ASonicCharacter::TurnTowardYaw(float DesiredYaw, float MaxDelta)
{
	if (!GetRootComponent())
		return;

	FRotator Rotation = GetActorRotation();
	const float Delta = FMath::UnwindDegrees(DesiredYaw - Rotation.Yaw);
	Rotation.Yaw = std::abs(Delta) <= MaxDelta ? DesiredYaw : FMath::NormalizeAxis(Rotation.Yaw + (Delta > 0.0f ? MaxDelta : -MaxDelta));
	Rotation.Pitch = 0.0f;
	Rotation.Roll = 0.0f;
	GetRootComponent()->SetRelativeRotation(Rotation);
}

USonicAnimInstance* ASonicCharacter::GetSonicAnimInstance() const
{
	return Cast<USonicAnimInstance>(SkeletalMeshComponent->GetAnimInstance());
}

void ASonicCharacter::EnsureCoinTemplate()
{
	if (!CoinParticle || CoinParticle->GetTemplate() || CoinParticlePath.empty())
		return;

	if (UParticleSystem* Template = UAssetManager::LoadParticleSystem(CoinParticlePath))
	{
		CoinParticle->SetTemplate(Template);
	}
}
