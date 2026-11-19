#include "EnginePCH.h"
#include "GameFramework/Actor/GundamCharacter.h"
#include "GameFramework/Actor/GundamBeam.h"
#include "GameFramework/Actor/GundamFunnel.h"
#include "GameFramework/Animation/GundamAnimInstance.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CharacterMovementComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Asset/AssetManager.h"
#include "Core/Misc/Paths.h"
#include "Engine/World.h"
#include "Input/InputSystem.h"
#include "Input/KeyCode.h"
#include "Render/Buffer.h"
#include "Render/Material.h"
#include "Render/RenderCommand.h"
#include "Render/RenderResourceManager.h"
#include "Render/Texture2D.h"

#include <algorithm>
#include <cmath>

namespace
{
	void EnsureThrusterMaterialsRegistered()
	{
		if (!UAssetManager::FindLoadedAsset("GundamThrusterGlow"))
		{
			UTexture2D* GlowTex = UAssetManager::GetAssetByPath<UTexture2D>("Textures/ThrusterGlow.png");
			if (!GlowTex)
			{
				GlowTex = UAssetManager::GetAssetByPath<UTexture2D>("Engine/Textures/BeamGlow.png");
			}
			if (GlowTex)
			{
				UMaterial* GlowMat = NewObject<UMaterial>();
				GlowMat->PSOType = EPSOType::Particle_Additive;
				GlowMat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/ParticleSpriteShader.hlsl");
				GlowMat->Textures.Add(GlowTex);
				GlowMat->BlendState = EBlendState::Additive;
				GlowMat->DepthStencilState = EDepthStencilState::ReadOnly;
				GlowMat->SamplerState = ESamplerState::LinearClamp;
				UAssetManager::Get().RegisterAsset("GundamThrusterGlow", GlowMat);
			}
		}

		if (!UAssetManager::FindLoadedAsset("GundamThrusterConeAdditive"))
		{
			UTexture2D* FadeTex = UAssetManager::GetAssetByPath<UTexture2D>("Textures/ThrusterConeFade.png");
			if (!FadeTex)
			{
				FadeTex = UAssetManager::GetAssetByPath<UTexture2D>("WhiteTexture");
			}
			if (FadeTex)
			{
				UMaterial* ConeMat = NewObject<UMaterial>();
				ConeMat->PSOType = EPSOType::StaticMesh_Additive;
				ConeMat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/StaticMeshTranslucentShader.hlsl");
				ConeMat->Textures.Add(FadeTex);
				ConeMat->BlendState = EBlendState::Additive;
				ConeMat->DepthStencilState = EDepthStencilState::ReadOnly;
				ConeMat->SamplerState = ESamplerState::LinearClamp;
				ConeMat->ParamLayout = EMaterialParamLayout::StaticMesh;
				ConeMat->ParamBuffer = RenderCommand::CreateConstantBuffer(sizeof(FStaticMeshMaterialParams));
				UAssetManager::Get().RegisterAsset("GundamThrusterConeAdditive", ConeMat);
			}
		}

		if (!UAssetManager::FindLoadedAsset("GundamShockwaveAdditive"))
		{
			UTexture2D* RingTex = UAssetManager::GetAssetByPath<UTexture2D>("Textures/ShockwaveRing.png");
			if (!RingTex)
			{
				RingTex = UAssetManager::GetAssetByPath<UTexture2D>("Textures/ThrusterGlow.png");
			}
			if (RingTex)
			{
				UMaterial* ShockMat = NewObject<UMaterial>();
				ShockMat->PSOType = EPSOType::Particle_Additive;
				ShockMat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/ParticleSpriteShader.hlsl");
				ShockMat->Textures.Add(RingTex);
				ShockMat->BlendState = EBlendState::Additive;
				ShockMat->DepthStencilState = EDepthStencilState::ReadOnly;
				ShockMat->SamplerState = ESamplerState::LinearClamp;
				UAssetManager::Get().RegisterAsset("GundamShockwaveAdditive", ShockMat);
			}
		}

		if (!UAssetManager::FindLoadedAsset("GundamWingBeamAdditive"))
		{
			UTexture2D* WingTex = UAssetManager::GetAssetByPath<UTexture2D>("Textures/WingBeam.png");
			if (!WingTex)
			{
				WingTex = UAssetManager::GetAssetByPath<UTexture2D>("Engine/Textures/BeamGlow.png");
			}
			if (WingTex)
			{
				UMaterial* WingMat = NewObject<UMaterial>();
				WingMat->PSOType = EPSOType::Particle_Additive;
				WingMat->Shader = FRenderResourceManager::GetShaderProgram("EngineResources/Shaders/ParticleSpriteShader.hlsl");
				WingMat->Textures.Add(WingTex);
				WingMat->BlendState = EBlendState::ParticleAdditive;
				WingMat->DepthStencilState = EDepthStencilState::ReadOnly;
				WingMat->SamplerState = ESamplerState::LinearClamp;
				UAssetManager::Get().RegisterAsset("GundamWingBeamAdditive", WingMat);
			}
		}
	}
}

AGundamCharacter::AGundamCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	CapsuleComponent->CapsuleHalfHeight = 1.0f;
	CapsuleComponent->CapsuleRadius = 0.5f;

	SkeletalMeshComponent->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -0.11f), FRotator(0.0f, 0.0f, 0.0f));
	SkeletalMeshComponent->SetRelativeScale3D(FVector(0.085f, 0.085f, 0.085f));

	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArmComponent->SetupAttachment(CapsuleComponent);
	SpringArmComponent->TargetArmLength = 7.2f;
	SpringArmComponent->TargetOffset = FVector(0.0f, 0.0f, 1.45f);
	SpringArmComponent->SocketOffset = FVector(0.0f, 0.35f, 0.0f);
	SpringArmComponent->bUsePawnControlRotation = true;
	SpringArmComponent->bInheritPitch = true;
	SpringArmComponent->bInheritYaw = true;
	SpringArmComponent->bInheritRoll = true;
	SpringArmComponent->bEnableCameraLag = false;
	SpringArmComponent->bEnableCameraRotationLag = false;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->SetExternalInputManaged(true);
	CameraComponent->bUsePawnControlRotation = false;

	// WASD 이동 시 몸을 돌리지 않고, 마우스 회전에만 캐릭터 정면을 맞춘다
	CharacterMovementComponent->bOrientRotationToMovement = false;
	CharacterMovementComponent->MaxWalkSpeed = NormalWalkSpeed;
	CharacterMovementComponent->MaxAcceleration = 4500.0f;
	CharacterMovementComponent->BrakingDecelerationWalking = 4500.0f;
	CharacterMovementComponent->GroundFriction = 14.0f;
	CharacterMovementComponent->GravityScale = 1.0f;
	CharacterMovementComponent->AirControl = 1.0f;
	CharacterMovementComponent->TerminalVelocity = 2200.0f;
	CharacterMovementComponent->JumpZVelocity = 420.0f;

	BackThrusterParticle = CreateDefaultSubobject<UParticleSystemComponent>("BackThrusterParticle");
	BackThrusterParticle->SetupAttachment(CapsuleComponent);
	BackThrusterParticle->SetRelativeLocation(FVector(-0.18f, 0.0f, 0.30f));

	RightFootThrusterParticle = CreateDefaultSubobject<UParticleSystemComponent>("RightFootThrusterParticle");
	RightFootThrusterParticle->SetupAttachment(CapsuleComponent);
	RightFootThrusterParticle->SetRelativeLocation(FVector(-0.04f, 0.11f, -0.88f));

	LeftFootThrusterParticle = CreateDefaultSubobject<UParticleSystemComponent>("LeftFootThrusterParticle");
	LeftFootThrusterParticle->SetupAttachment(CapsuleComponent);
	LeftFootThrusterParticle->SetRelativeLocation(FVector(-0.04f, -0.11f, -0.88f));

	WingUpperLeftParticle = CreateDefaultSubobject<UParticleSystemComponent>("WingUpperLeftParticle");
	WingUpperLeftParticle->SetupAttachment(CapsuleComponent);

	WingLowerLeftParticle = CreateDefaultSubobject<UParticleSystemComponent>("WingLowerLeftParticle");
	WingLowerLeftParticle->SetupAttachment(CapsuleComponent);

	WingUpperRightParticle = CreateDefaultSubobject<UParticleSystemComponent>("WingUpperRightParticle");
	WingUpperRightParticle->SetupAttachment(CapsuleComponent);

	WingLowerRightParticle = CreateDefaultSubobject<UParticleSystemComponent>("WingLowerRightParticle");
	WingLowerRightParticle->SetupAttachment(CapsuleComponent);

	DashShockwaveParticle = CreateDefaultSubobject<UParticleSystemComponent>("DashShockwaveParticle");
	DashShockwaveParticle->SetupAttachment(CapsuleComponent);
	DashShockwaveParticle->SetAutoActivate(false);

	if (FPaths::HasProject())
	{
		EnsureThrusterMaterialsRegistered();
		if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>("FBX/Strike_Freedom/Strike_Freedom.fbx.bin"))
		{
			SkeletalMeshComponent->SetSkeletalMesh(Mesh);
		}
		if (UParticleSystem* BackTemplate = UAssetManager::LoadParticleSystem("Particles/BackThruster.particle"))
		{
			BackThrusterParticle->InitializeSystem(BackTemplate);
		}
		if (UParticleSystem* FootTemplate = UAssetManager::LoadParticleSystem("Particles/FootThruster.particle"))
		{
			RightFootThrusterParticle->InitializeSystem(FootTemplate);
			LeftFootThrusterParticle->InitializeSystem(FootTemplate);
		}
		if (UParticleSystem* WingTemplate = UAssetManager::LoadParticleSystem("Particles/WingLight.particle"))
		{
			WingUpperLeftParticle->InitializeSystem(WingTemplate);
			WingLowerLeftParticle->InitializeSystem(WingTemplate);
			WingUpperRightParticle->InitializeSystem(WingTemplate);
			WingLowerRightParticle->InitializeSystem(WingTemplate);
		}
		if (UParticleSystem* ShockTemplate = UAssetManager::LoadParticleSystem("Particles/BoostShockwave.particle"))
		{
			DashShockwaveParticle->InitializeSystem(ShockTemplate);
		}
	}
}

void AGundamCharacter::BeginPlay()
{
	Super::BeginPlay();
	EnsureVisualAssets();

	if (APlayerController* PC = GetController())
	{
		PC->ControlRotation.Pitch = 10.0f;
	}
}

void AGundamCharacter::EnsureVisualAssets()
{
	if (!SkeletalMeshComponent)
	{
		return;
	}

	EnsureThrusterMaterialsRegistered();

	if (!SkeletalMeshComponent->GetSkeletalMesh())
	{
		if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>("FBX/Strike_Freedom/Strike_Freedom.fbx.bin"))
		{
			SkeletalMeshComponent->SetSkeletalMesh(Mesh);
		}
	}

	if (SkeletalMeshComponent->GetAnimBlueprintPath().empty() && !SkeletalMeshComponent->GetAnimInstance())
	{
		UGundamAnimInstance* AnimInstance = NewObject<UGundamAnimInstance>(SkeletalMeshComponent);
		SkeletalMeshComponent->SetAnimInstance(AnimInstance);
	}

	BackBoneIndex = SkeletalMeshComponent->FindBoneIndex("上半身2");
	RightFootBoneIndex = SkeletalMeshComponent->FindBoneIndex("右足首");
	LeftFootBoneIndex = SkeletalMeshComponent->FindBoneIndex("左足首");
	WingLeftBoneIndex = SkeletalMeshComponent->FindBoneIndex("Wing_L");
	WingRightBoneIndex = SkeletalMeshComponent->FindBoneIndex("Wing_R");

	if (BackThrusterParticle && !BackThrusterParticle->GetTemplate())
	{
		if (UParticleSystem* BackTemplate = UAssetManager::LoadParticleSystem("Particles/BackThruster.particle"))
		{
			BackThrusterParticle->SetTemplate(BackTemplate);
		}
	}
	if (UParticleSystem* FootTemplate = UAssetManager::LoadParticleSystem("Particles/FootThruster.particle"))
	{
		if (RightFootThrusterParticle && !RightFootThrusterParticle->GetTemplate())
		{
			RightFootThrusterParticle->SetTemplate(FootTemplate);
		}
		if (LeftFootThrusterParticle && !LeftFootThrusterParticle->GetTemplate())
		{
			LeftFootThrusterParticle->SetTemplate(FootTemplate);
		}
	}
	if (UParticleSystem* WingTemplate = UAssetManager::LoadParticleSystem("Particles/WingLight.particle"))
	{
		UParticleSystemComponent* WingComps[4] = {
			WingUpperLeftParticle, WingLowerLeftParticle,
			WingUpperRightParticle, WingLowerRightParticle
		};
		for (UParticleSystemComponent* Comp : WingComps)
		{
			if (Comp && !Comp->GetTemplate())
			{
				Comp->SetTemplate(WingTemplate);
			}
		}
	}
	if (DashShockwaveParticle && !DashShockwaveParticle->GetTemplate())
	{
		if (UParticleSystem* ShockTemplate = UAssetManager::LoadParticleSystem("Particles/BoostShockwave.particle"))
		{
			DashShockwaveParticle->SetTemplate(ShockTemplate);
		}
	}
}

FRotator AGundamCharacter::GetMovementInputRotation() const
{
	if (const APlayerController* PC = GetController())
	{
		return FRotator(0.0f, PC->ControlRotation.Yaw, 0.0f);
	}
	return Super::GetMovementInputRotation();
}

void AGundamCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	const APlayerController* PC = GetController();
	const bool bSpaceDown = PC && PC->IsKeyDown(static_cast<int32>(EKeyCode::Space));
	const bool bShiftDown = PC && PC->IsKeyDown(static_cast<int32>(EKeyCode::Shift));

	UpdateControlRotationSync();
	UpdateDashInput(DeltaTime);
	UpdateAirDamping(DeltaTime);
	UpdateBoosterAndDescend(DeltaTime, bSpaceDown, bShiftDown);

	const bool bBoosting = (bSpaceDown && !bShiftDown) || (DashKeyIndex >= 0);
	if (UGundamAnimInstance* AnimInstance = Cast<UGundamAnimInstance>(SkeletalMeshComponent ? SkeletalMeshComponent->GetAnimInstance() : nullptr))
	{
		AnimInstance->SetWingBoostTarget((DashKeyIndex >= 0) ? 1.0f : (bBoosting ? 0.65f : 0.0f));
	}
	UpdateMeshTilt(DeltaTime, bBoosting, bShiftDown);
	UpdateThrusters(DeltaTime, bBoosting);
	UpdateWingEffects(DeltaTime, bBoosting);
	HandleWeaponInput();

	if (CurrentWeapon == EGundamWeapon::Funnel && OrbitingFunnels.IsEmpty() && FunnelRespawnTimer > 0.0f)
	{
		FunnelRespawnTimer -= DeltaTime;
		if (FunnelRespawnTimer <= 0.0f)
		{
			FunnelRespawnTimer = -1.0f;
			DeployOrbitingFunnels();
		}
	}
	UpdateOrbitingFunnels(DeltaTime);
}

void AGundamCharacter::UpdateThrusters(float DeltaTime, bool bBoosting)
{
	if (!SkeletalMeshComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	const float HorizontalSpeed = FVector(GetVelocity().X, GetVelocity().Y, 0.0f).Size();
	const bool bMoving = HorizontalSpeed > 0.6f;
	const float TargetScale = (DashKeyIndex >= 0) ? 1.85f : (bBoosting ? 1.55f : (bMoving ? 1.25f : 0.90f));
	const float Alpha = 1.0f - std::exp(-12.0f * DeltaTime);
	CurrentThrusterScale += (TargetScale - CurrentThrusterScale) * Alpha;
	const FVector ScaleVec(CurrentThrusterScale, CurrentThrusterScale, CurrentThrusterScale);

	const FRotator MeshWorldRot = SkeletalMeshComponent->GetWorldRotation();
	const FQuat MeshQuat = MeshWorldRot.Quaternion();
	const FVector MeshForward = MeshQuat.GetForwardVector();
	const FVector MeshUp = MeshQuat.GetUpVector();

	if (BackThrusterParticle)
	{
		if (BackBoneIndex >= 0)
		{
			const FVector BonePos = SkeletalMeshComponent->GetBoneWorldMatrix(BackBoneIndex).TransformPosition(FVector::ZeroVector);
			BackThrusterParticle->SetWorldLocationAndRotation(BonePos - MeshForward * 0.24f + MeshUp * 0.10f, MeshWorldRot, false);
		}
		else
		{
			BackThrusterParticle->SetWorldRotation(MeshWorldRot, false);
		}
		BackThrusterParticle->SetRelativeScale3D(ScaleVec);
	}

	if (RightFootThrusterParticle)
	{
		if (RightFootBoneIndex >= 0)
		{
			const FVector BonePos = SkeletalMeshComponent->GetBoneWorldMatrix(RightFootBoneIndex).TransformPosition(FVector::ZeroVector);
			RightFootThrusterParticle->SetWorldLocationAndRotation(BonePos - MeshUp * 0.13f - MeshForward * 0.10f, MeshWorldRot, false);
		}
		else
		{
			RightFootThrusterParticle->SetWorldRotation(MeshWorldRot, false);
		}
		RightFootThrusterParticle->SetRelativeScale3D(ScaleVec);
	}

	if (LeftFootThrusterParticle)
	{
		if (LeftFootBoneIndex >= 0)
		{
			const FVector BonePos = SkeletalMeshComponent->GetBoneWorldMatrix(LeftFootBoneIndex).TransformPosition(FVector::ZeroVector);
			LeftFootThrusterParticle->SetWorldLocationAndRotation(BonePos - MeshUp * 0.13f - MeshForward * 0.10f, MeshWorldRot, false);
		}
		else
		{
			LeftFootThrusterParticle->SetWorldRotation(MeshWorldRot, false);
		}
		LeftFootThrusterParticle->SetRelativeScale3D(ScaleVec);
	}
}

void AGundamCharacter::UpdateWingEffects(float DeltaTime, bool bBoosting)
{
	(void)DeltaTime;
	(void)bBoosting;
	if (!SkeletalMeshComponent)
	{
		return;
	}

	// 부스트·대쉬 여부와 무관하게 날개 파티클 크기는 항상 일정하게 유지
	CurrentWingScale = 0.89f;

	UParticleSystemComponent* WingComps[4] = {
		WingUpperLeftParticle, WingLowerLeftParticle,
		WingUpperRightParticle, WingLowerRightParticle
	};

	const FQuat MeshQuat = SkeletalMeshComponent->GetWorldRotation().Quaternion();
	const FVector MeshForward = MeshQuat.GetForwardVector();
	const FVector MeshRight = MeshQuat.GetRightVector();
	const FVector MeshUp = MeshQuat.GetUpVector();

	const FVector BaseCenter = (BackBoneIndex >= 0)
		? SkeletalMeshComponent->GetBoneWorldMatrix(BackBoneIndex).TransformPosition(FVector::ZeroVector)
		: (GetActorLocation() + MeshUp * 0.35f);
	const FVector WingLPos = (WingLeftBoneIndex >= 0)
		? SkeletalMeshComponent->GetBoneWorldMatrix(WingLeftBoneIndex).TransformPosition(FVector::ZeroVector)
		: (BaseCenter - MeshForward * 0.22f - MeshRight * 0.16f + MeshUp * 0.14f);
	const FVector WingRPos = (WingRightBoneIndex >= 0)
		? SkeletalMeshComponent->GetBoneWorldMatrix(WingRightBoneIndex).TransformPosition(FVector::ZeroVector)
		: (BaseCenter - MeshForward * 0.22f + MeshRight * 0.16f + MeshUp * 0.14f);

	// 4매 날개(좌상/좌하/우상/우하) 각각의 기점과 실제 FBX 날개 각도에 맞춘 빔 방향
	for (int32 i = 0; i < 4; ++i)
	{
		UParticleSystemComponent* Comp = WingComps[i];
		if (!Comp)
		{
			continue;
		}

		const bool bRightSide = (i >= 2);
		const bool bUpperWing = (i % 2 == 0);
		const float SideSign = bRightSide ? 1.0f : -1.0f;
		const FVector RootPos = bRightSide ? WingRPos : WingLPos;

		const FVector Origin = bUpperWing
			? (RootPos - MeshForward * 0.12f + MeshRight * (SideSign * 0.24f) + MeshUp * 0.08f)
			: (RootPos - MeshForward * 0.12f + MeshRight * (SideSign * 0.18f) - MeshUp * 0.02f);
		const FVector BeamDir = (bUpperWing
			? (-MeshForward * 0.26f + MeshRight * (SideSign * 0.93f) + MeshUp * 0.24f)
			: (-MeshForward * 0.26f + MeshRight * (SideSign * 0.80f) - MeshUp * 0.54f)).GetSafeNormal();

		FRotator WingRot;
		WingRot.LookDirection(BeamDir);
		Comp->SetWorldLocationAndRotation(Origin, WingRot, false);

		const float BladeScale = CurrentWingScale * (bUpperWing ? 1.0f : 0.85f);
		Comp->SetRelativeScale3D(FVector(BladeScale, BladeScale, BladeScale));
	}
}

void AGundamCharacter::TriggerDashShockwave()
{
	if (!DashShockwaveParticle)
	{
		return;
	}

	const FVector ShockPos = GetActorLocation() + FVector(0.0f, 0.0f, 0.25f);
	DashShockwaveParticle->SetWorldLocation(ShockPos, false);
	// SetAutoActivate(false)로 틱이 꺼져 있으므로 Activate(true)로 컴포넌트 틱과 에미터를 함께 재시작한다.
	DashShockwaveParticle->Activate(true);
}

void AGundamCharacter::UpdateControlRotationSync()
{
	const APlayerController* PC = GetController();
	if (!PC || !CapsuleComponent)
	{
		return;
	}

	CapsuleComponent->SetRelativeRotation(FRotator(0.0f, PC->ControlRotation.Yaw, 0.0f));
}

void AGundamCharacter::UpdateDashInput(float DeltaTime)
{
	const APlayerController* PC = GetController();
	if (!PC || !CharacterMovementComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	InputClock += DeltaTime;

	static constexpr EKeyCode MoveKeys[4] = {
		EKeyCode::W,
		EKeyCode::S,
		EKeyCode::D,
		EKeyCode::A
	};

	const FQuat YawQuat = FRotator(0.0f, PC->ControlRotation.Yaw, 0.0f).Quaternion();
	const FVector DirTable[4] = {
		YawQuat.GetForwardVector(),
		-YawQuat.GetForwardVector(),
		YawQuat.GetRightVector(),
		-YawQuat.GetRightVector()
	};

	bool bKeyDown[4] = {};
	for (int32 i = 0; i < 4; ++i)
	{
		bKeyDown[i] = PC->IsKeyDown(static_cast<int32>(MoveKeys[i]));
		if (bKeyDown[i] && !bWasMoveKeyDown[i])
		{
			if (InputClock - LastTapTime[i] <= DoubleTapWindow)
			{
				DashKeyIndex = i;
				DashElapsed = 0.0f;

				// 연타 후 꾹 누르는 동안 즉시 부스트 속도로 진입하도록 초기 추진 속도 부여
				const float BurstSpeedWorld = DashSpeed * CmToWorld * 0.78f;
				const FVector DashDir = DirTable[i];
				CharacterMovementComponent->Velocity.X = DashDir.X * BurstSpeedWorld;
				CharacterMovementComponent->Velocity.Y = DashDir.Y * BurstSpeedWorld;
				TriggerDashShockwave();
			}
			LastTapTime[i] = InputClock;
		}
		bWasMoveKeyDown[i] = bKeyDown[i];
	}

	if (DashKeyIndex >= 0)
	{
		if (bKeyDown[DashKeyIndex])
		{
			DashElapsed += DeltaTime;
		}
		else
		{
			DashKeyIndex = -1;
			DashElapsed = 0.0f;
		}
	}

	CharacterMovementComponent->MaxWalkSpeed = (DashKeyIndex >= 0) ? DashSpeed : NormalWalkSpeed;
}

void AGundamCharacter::UpdateAirDamping(float DeltaTime)
{
	const APlayerController* PC = GetController();
	if (!PC || !CharacterMovementComponent || !CharacterMovementComponent->IsFalling() || DeltaTime <= 0.0f)
	{
		return;
	}

	// 엔진 PhysFalling은 마찰이 0이라 공중 관성이 우주처럼 남으므로 직접 감쇠 적용
	const float ForwardInput =
		(PC->IsKeyDown(static_cast<int32>(EKeyCode::W)) ? 1.0f : 0.0f) -
		(PC->IsKeyDown(static_cast<int32>(EKeyCode::S)) ? 1.0f : 0.0f);
	const float RightInput =
		(PC->IsKeyDown(static_cast<int32>(EKeyCode::D)) ? 1.0f : 0.0f) -
		(PC->IsKeyDown(static_cast<int32>(EKeyCode::A)) ? 1.0f : 0.0f);

	const FQuat YawQuat = FRotator(0.0f, PC->ControlRotation.Yaw, 0.0f).Quaternion();
	FVector DesiredDir = YawQuat.GetForwardVector() * ForwardInput + YawQuat.GetRightVector() * RightInput;
	DesiredDir.Z = 0.0f;

	const float DampFactor = std::exp(-AirStopDamping * DeltaTime);
	FVector HorizVel(CharacterMovementComponent->Velocity.X, CharacterMovementComponent->Velocity.Y, 0.0f);

	if (DesiredDir.SizeSquared() < 0.001f && DashKeyIndex < 0)
	{
		HorizVel = HorizVel * DampFactor;
	}
	else if (DesiredDir.SizeSquared() >= 0.001f)
	{
		DesiredDir = DesiredDir.GetSafeNormal();
		const float ForwardSpeed = HorizVel.Dot(DesiredDir);
		const FVector ParallelVel = DesiredDir * std::max(ForwardSpeed, 0.0f);
		const FVector DriftVel = HorizVel - ParallelVel;
		HorizVel = ParallelVel + DriftVel * DampFactor;
	}

	CharacterMovementComponent->Velocity.X = HorizVel.X;
	CharacterMovementComponent->Velocity.Y = HorizVel.Y;
}

void AGundamCharacter::UpdateBoosterAndDescend(float DeltaTime, bool bSpaceDown, bool bShiftDown)
{
	if (!CharacterMovementComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	CharacterMovementComponent->TerminalVelocity = bShiftDown ? 4000.0f : 2200.0f;

	if (bShiftDown)
	{
		if (CharacterMovementComponent->IsFalling())
		{
			CharacterMovementComponent->Velocity.Z = std::max(
				CharacterMovementComponent->Velocity.Z - FastDescendAccel * DeltaTime,
				-MaxFastDescendSpeed);
		}
		return;
	}

	if (bSpaceDown)
	{
		if (CharacterMovementComponent->IsMovingOnGround())
		{
			CharacterMovementComponent->SetMovementMode(ECharacterMovementMode::Falling);
			CharacterMovementComponent->Velocity.Z = std::max(CharacterMovementComponent->Velocity.Z, 3.0f);
		}

		// 공중에서 Space를 누르면 중력을 이기고 즉시 상승 부스터 작동
		const float EffectiveGravity = CharacterMovementComponent->Gravity * CharacterMovementComponent->GravityScale;
		CharacterMovementComponent->Velocity.Z = std::min(
			CharacterMovementComponent->Velocity.Z + (BoosterUpAccel + EffectiveGravity) * DeltaTime,
			MaxBoosterUpSpeed);
	}
}

void AGundamCharacter::UpdateMeshTilt(float DeltaTime, bool bBoosting, bool bDescending)
{
	if (!SkeletalMeshComponent || DeltaTime <= 0.0f)
	{
		return;
	}

	const FVector Velocity = GetVelocity();
	const FQuat ActorQuat = GetActorQuat();
	const FVector Forward = ActorQuat.GetForwardVector();
	const FVector Right = ActorQuat.GetRightVector();

	const float MaxSpeedWorld = std::max(CharacterMovementComponent->MaxWalkSpeed * CmToWorld, 1.0f);
	const float ForwardRatio = std::clamp(Velocity.Dot(Forward) / MaxSpeedWorld, -1.0f, 1.0f);
	const float RightRatio = std::clamp(Velocity.Dot(Right) / MaxSpeedWorld, -1.0f, 1.0f);

	// +Pitch는 앞(+X)으로 숙여지고, -Roll은 오른쪽(+Y)으로 기운다
	float TargetPitch = ForwardRatio * MaxTiltAngle;
	if (bBoosting && ForwardRatio >= -0.05f)
	{
		TargetPitch = std::min(TargetPitch + 5.0f, MaxTiltAngle + 4.0f);
	}
	else if (bDescending && CharacterMovementComponent->IsFalling())
	{
		TargetPitch = std::max(TargetPitch - 6.0f, -MaxTiltAngle);
	}
	const float TargetRoll = -RightRatio * MaxTiltAngle;

	const float Alpha = 1.0f - std::exp(-TiltInterpSpeed * DeltaTime);
	CurrentMeshTilt.Pitch += (TargetPitch - CurrentMeshTilt.Pitch) * Alpha;
	CurrentMeshTilt.Yaw = 0.0f;
	CurrentMeshTilt.Roll += (TargetRoll - CurrentMeshTilt.Roll) * Alpha;

	SkeletalMeshComponent->SetRelativeRotation(CurrentMeshTilt);
}

void AGundamCharacter::HandleWeaponInput()
{
	const APlayerController* PC = GetController();
	if (!PC)
	{
		return;
	}

	const bool bNum1Down = PC->IsKeyDown(static_cast<int32>(EKeyCode::Num1)) || FInputSystem::IsKeyDown(EKeyCode::Num1);
	const bool bNum2Down = PC->IsKeyDown(static_cast<int32>(EKeyCode::Num2)) || FInputSystem::IsKeyDown(EKeyCode::Num2);
	const bool bLButtonDown = PC->IsKeyDown(static_cast<int32>(EKeyCode::LButton));

	if (bNum1Down && !bWasNum1Down)
	{
		CurrentWeapon = EGundamWeapon::BeamRifle;
		FunnelRespawnTimer = -1.0f;
		ClearOrbitingFunnels();
		KRS_LOG(Info, "[Gundam] Weapon switched: 1 (Beam Rifle)");
	}
	if (bNum2Down && !bWasNum2Down)
	{
		CurrentWeapon = EGundamWeapon::Funnel;
		FunnelRespawnTimer = -1.0f;
		if (OrbitingFunnels.IsEmpty())
		{
			DeployOrbitingFunnels();
		}
		KRS_LOG(Info, "[Gundam] Weapon switched: 2 (Super Dragoon Funnel)");
	}

	if (bLButtonDown && !bWasLButtonDown)
	{
		if (CurrentWeapon == EGundamWeapon::BeamRifle)
		{
			FireBeam();
		}
		else
		{
			FireFunnels();
		}
	}

	bWasNum1Down = bNum1Down;
	bWasNum2Down = bNum2Down;
	bWasLButtonDown = bLButtonDown;
}

FVector AGundamCharacter::ComputeFunnelOrbitSlot(int32 Index) const
{
	const APlayerController* PC = GetController();
	const float Yaw = PC ? PC->ControlRotation.Yaw : GetActorRotation().Yaw;
	const FQuat YawQuat = FRotator(0.0f, Yaw, 0.0f).Quaternion();
	const FVector Forward = YawQuat.GetForwardVector();
	const FVector Right = YawQuat.GetRightVector();
	const FVector Up(0.0f, 0.0f, 1.0f);

	// 캐릭터 기준 YZ 평면(좌 3개, 우 3개)에 대칭 정렬
	const int32 Count = std::max(FunnelCount, 1);
	const int32 Half = std::max((Count + 1) / 2, 1);
	const bool bRightSide = (Index >= Half);
	const float SideSign = bRightSide ? 1.0f : -1.0f;
	const int32 SideCount = bRightSide ? (Count - Half) : Half;
	const int32 Row = bRightSide ? (Index - Half) : Index;

	const float RowT = (SideCount <= 1) ? 0.5f : static_cast<float>(Row) / static_cast<float>(SideCount - 1);
	// 가운데 줄이 바깥쪽으로 살짝 넓은 부채형 YZ 배치 (좌/우 각각 상·중·하)
	const float ArchOffset = 1.0f - std::abs(RowT - 0.5f) * 0.45f;
	const float SideDist = SideSign * FunnelOrbitRadius * ArchOffset * 1.08f;
	const float Height = (0.5f - RowT) * 1.25f + 0.30f;
	const float HoverBob = 0.025f * std::sin(FunnelOrbitTime * 2.5f + static_cast<float>(Index) * 1.0471975f);

	return GetActorLocation()
		- Forward * 0.15f
		+ Right * SideDist
		+ Up * (Height + HoverBob);
}

void AGundamCharacter::DeployOrbitingFunnels()
{
	UWorld* World = GetWorld();
	const APlayerController* PC = GetController();
	if (!World || FunnelCount <= 0)
	{
		return;
	}

	EnsureThrusterMaterialsRegistered();
	ClearOrbitingFunnels();

	const FRotator AimRot = PC
		? FRotator(std::clamp(PC->ControlRotation.Pitch, -60.0f, 60.0f), PC->ControlRotation.Yaw, 0.0f)
		: GetActorRotation();
	const FQuat AimQuat = AimRot.Quaternion();

	for (int32 i = 0; i < FunnelCount; ++i)
	{
		const FVector SlotPos = ComputeFunnelOrbitSlot(i);
		const FTransform SpawnTransform(SlotPos, FVector(1.0f, 1.0f, 1.0f), AimQuat);
		if (AGundamFunnel* Funnel = World->SpawnActor<AGundamFunnel>(NAME_None, &SpawnTransform))
		{
			OrbitingFunnels.Add(Funnel);
		}
	}
}

void AGundamCharacter::ClearOrbitingFunnels()
{
	for (AGundamFunnel* Funnel : OrbitingFunnels)
	{
		if (Funnel && !Funnel->IsLaunched())
		{
			Funnel->Destroy();
		}
	}
	OrbitingFunnels.Empty();
}

void AGundamCharacter::UpdateOrbitingFunnels(float DeltaTime)
{
	if (OrbitingFunnels.IsEmpty() || DeltaTime <= 0.0f)
	{
		return;
	}

	FunnelOrbitTime += DeltaTime;

	const APlayerController* PC = GetController();
	const FRotator AimRot = PC
		? FRotator(std::clamp(PC->ControlRotation.Pitch, -60.0f, 60.0f), PC->ControlRotation.Yaw, 0.0f)
		: GetActorRotation();
	const float Alpha = 1.0f - std::exp(-22.0f * DeltaTime);

	for (int32 i = 0; i < OrbitingFunnels.Num(); ++i)
	{
		AGundamFunnel* Funnel = OrbitingFunnels[i];
		if (!Funnel || Funnel->IsLaunched())
		{
			continue;
		}

		const FVector TargetPos = ComputeFunnelOrbitSlot(i);
		const FVector CurPos = Funnel->GetActorLocation();
		const FVector NextPos = CurPos + (TargetPos - CurPos) * Alpha;
		if (UStaticMeshComponent* FunnelRoot = Funnel->GetStaticMeshComponent())
		{
			FunnelRoot->SetWorldLocationAndRotation(NextPos, AimRot, false);
		}
	}
}

void AGundamCharacter::FireBeam()
{
	UWorld* World = GetWorld();
	const APlayerController* PC = GetController();
	if (!World)
	{
		return;
	}

	if (UGundamAnimInstance* AnimInstance = Cast<UGundamAnimInstance>(SkeletalMeshComponent ? SkeletalMeshComponent->GetAnimInstance() : nullptr))
	{
		AnimInstance->RequestShoot();
	}

	const FRotator AimRot = PC
		? FRotator(std::clamp(PC->ControlRotation.Pitch, -60.0f, 60.0f), PC->ControlRotation.Yaw, 0.0f)
		: GetActorRotation();
	const FQuat AimQuat = AimRot.Quaternion();
	const FVector Forward = AimQuat.GetForwardVector();
	const FVector Right = AimQuat.GetRightVector();
	const FVector Up = AimQuat.GetUpVector();

	const FVector SpawnPos = GetActorLocation() + Forward * 1.40f + Right * 0.30f + Up * 0.40f;
	const FTransform SpawnTransform(SpawnPos, FVector(1.0f, 1.0f, 1.0f), AimQuat);
	World->SpawnActor<AGundamBeam>(NAME_None, &SpawnTransform);
}

void AGundamCharacter::FireFunnels()
{
	if (OrbitingFunnels.IsEmpty())
	{
		return;
	}

	const APlayerController* PC = GetController();
	const FRotator AimRot = PC
		? FRotator(std::clamp(PC->ControlRotation.Pitch, -60.0f, 60.0f), PC->ControlRotation.Yaw, 0.0f)
		: GetActorRotation();
	const FQuat AimQuat = AimRot.Quaternion();
	const FVector Forward = AimQuat.GetForwardVector();
	const FVector CharCenter = GetActorLocation() + FVector(0.0f, 0.0f, 0.35f);

	for (AGundamFunnel* Funnel : OrbitingFunnels)
	{
		if (!Funnel || Funnel->IsLaunched())
		{
			continue;
		}

		// 각 판넬 위치에서 전방으로 약간 퍼지며 날아가는 부채꼴 탄도
		const FVector Outward = (Funnel->GetActorLocation() - CharCenter).GetSafeNormal();
		const FVector LaunchDir = (Forward + Outward * 0.18f).GetSafeNormal();
		FRotator LaunchRot;
		LaunchRot.LookDirection(LaunchDir);
		if (UStaticMeshComponent* FunnelRoot = Funnel->GetStaticMeshComponent())
		{
			FunnelRoot->SetWorldRotation(LaunchRot, false);
		}
		Funnel->Launch(LaunchDir * FunnelSpeed, 1.15f, 0.35f);
	}

	OrbitingFunnels.Empty();
	FunnelRespawnTimer = FunnelRespawnDelay;
}
