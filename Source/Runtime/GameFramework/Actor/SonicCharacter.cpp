#include "EnginePCH.h"
#include "GameFramework/Actor/SonicCharacter.h"
#include "GameFramework/Animation/MyAnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Animation/AnimGraphInstance.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Particles/ParticleEmitterInstance.h"
#include <algorithm>

ASonicCharacter::ASonicCharacter()
{
	// 믹사모 메시는 cm 단위라 0.01배. 원점이 발이라 캡슐 바닥까지 내린다
	CapsuleComponent->CapsuleHalfHeight = 0.9f;
	CapsuleComponent->CapsuleRadius = 0.35f;
	SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -0.9f), FRotator(0.0f, 0.0f, 0.0f));
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.01f, 0.01f, 0.01f));

	// 제자리에 서 있는 샌드백이라 이동·회전하지 않는다
	CharacterMovementComponent->MaxWalkSpeed = 0.0f;
	CharacterMovementComponent->bOrientRotationToMovement = false;

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
	// AMyCharacter::BeginPlay는 경로가 빈 UMyAnimInstance를 끼우므로 건너뛰고, 소닉 클립 경로를 채운 인스턴스를 직접 만든다
	ACharacter::BeginPlay();

	EnsureCoinTemplate();

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !Cast<UAnimGraphInstance>(SkeletalMeshComponent->GetAnimInstance()))
	{
		// 그래프는 초기화(SetAnimInstance) 시점에 경로를 읽으므로 경로를 먼저 채운다
		UMyAnimInstance* Instance = NewObject<UMyAnimInstance>(SkeletalMeshComponent);
		Instance->IdleAnimationPath = IdleAnimationPath;
		Instance->HitAnimationPath = HitAnimationPath;
		SkeletalMeshComponent->SetAnimInstance(Instance);
	}
}

void ASonicCharacter::SetIsHit()
{
	Super::SetIsHit();

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
	for (FParticleEmitterInstance* Instance : CoinParticle->GetEmitterInstances())
	{
		if (Instance)
			Instance->ForceSpawn(Count, SpawnLocation, FVector::ZeroVector);
	}
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
