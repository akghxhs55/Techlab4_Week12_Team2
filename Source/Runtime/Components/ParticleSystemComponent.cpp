#include "EnginePCH.h"
#include "ParticleSystemComponent.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/Module/ParticleModuleTypeDataMesh.h"
#include "Particles/ParticleMeshEmitterInstance.h"
#include "Render/Mesh.h"
#include "Particles/ParticleHelper.h"
#include "Render/LineBatcher.h"
#include "UObject/UObjectIterator.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "Engine/ParticleSystemSceneProxy.h"

UParticleSystemComponent::UParticleSystemComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	bCastShadow = false;
}

UParticleSystemComponent::~UParticleSystemComponent()
{
	ResetParticles();
}

void UParticleSystemComponent::OnRegister()
{
	Super::OnRegister();

	// 에디터 월드는 BeginPlay가 없으므로 여기서 시작
	if (IsAutoActivate())
	{
		ActivateSystem();
	}
}

void UParticleSystemComponent::OnUnregister()
{
	ResetParticles();
	Super::OnUnregister();
}

void UParticleSystemComponent::Activate(bool bReset)
{
	Super::Activate(bReset);
	ActivateSystem(bReset);
}

void UParticleSystemComponent::Deactivate()
{
	// 부모가 Tick을 끄면 남은 파티클이 그 자리에 멈추므로 다시 켠다
	Super::Deactivate();
	SetComponentTickEnabled(true);
	DeactivateSystem();
}

void UParticleSystemComponent::InitializeSystem(UParticleSystem* InTemplate)
{
	ResetParticles();

	if (InTemplate)
	{
		Template = InTemplate;
	}
	if (!Template)
		return;

	for (UParticleEmitter* Emitter : Template->GetEmitters())
	{
		if (!Emitter)
			continue;
		if (FParticleEmitterInstance* Instance = Emitter->CreateInstance(this))
		{
			EmitterInstances.Add(Instance);
		}
	}
	bWasCompleted = false;

	// 새 인스턴스는 LOD 0으로 시작하므로 지금 LOD를 다시 적용한다 (DirectSet 값, 미리보기 유지)
	ApplyLODLevel(IsPreviewLODActive() ? PreviewLODOverride : LODLevel);
}

void UParticleSystemComponent::SetTemplate(UParticleSystem* NewTemplate)
{
	if (Template == NewTemplate && !EmitterInstances.IsEmpty())
	{
		return;
	}

	ResetParticles();
	Template = NewTemplate;
	bWasCompleted = false;

	if (!Template)
		return;

	if (IsRegistered() && (IsActive() || IsAutoActivate()))
	{
		ActivateSystem(true);
	}
	else
	{
		InitializeSystem();
	}
}

void UParticleSystemComponent::ActivateSystem(bool bReset)
{
	if (EmitterInstances.IsEmpty() || bReset)
	{
		InitializeSystem();
	}

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bSpawningEnabled = true;
	}
	bWasCompleted = false;

	// 첫 Tick 전에 거리로 정해 둔다. 멀리서 터진 폭발이 LOD 0 Burst로 시작하지 않게 한다
	LODCheckTimer = 0.0f;
	bPendingActivateLOD = false;
	if (Template && !IsPreviewLODActive() && Template->GetLODMethod() != EParticleSystemLODMethod::DirectSet)
	{
		const int32 DesiredLOD = ComputeDesiredLOD();
		if (DesiredLOD >= 0)
		{
			ApplyLODLevel(DesiredLOD);
		}
		else
		{
			// 아직 그려진 뷰가 없으면(시작 직후) 뷰가 생기는 첫 Tick에서 정한다
			bPendingActivateLOD = true;
		}
	}
}

void UParticleSystemComponent::DeactivateSystem()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->bSpawningEnabled = false;
	}
}

void UParticleSystemComponent::ResetParticles()
{
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		delete Instance;
	}
	EmitterInstances.Empty();
}

void UParticleSystemComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);

	if (EmitterInstances.IsEmpty())
		return;

	// 인스턴스가 이번 Tick을 새 LOD의 모듈로 돌도록 먼저 고른다
	UpdateLODLevel(DeltaTime);

	bool bAllComplete = true;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		Instance->Tick(DeltaTime);
		if (!Instance->IsComplete())
		{
			bAllComplete = false;
		}
	}
	bWasCompleted = bAllComplete;

	// 바운드는 Transform이 바뀔 때만 갱신되므로 매 프레임 요청
	if (FPrimitiveSceneProxy* Proxy = GetSceneProxy())
	{
		Proxy->GetScene()->MarkDirty(Proxy);
	}
}

void UParticleSystemComponent::SetLODLevel(int32 InLODLevel)
{
	ApplyLODLevel(InLODLevel);
}

void UParticleSystemComponent::SetPreviewLODOverride(int32 InLODLevel)
{
	PreviewLODOverride = InLODLevel < 0 ? -1 : InLODLevel;
	if (IsPreviewLODActive())
	{
		ApplyLODLevel(PreviewLODOverride);
	}
	else
	{
		// 미리보기를 끄면 다음 Tick에서 바로 거리로 다시 고르게 한다
		LODCheckTimer = Template ? Template->GetLODDistanceCheckTime() : 0.0f;
	}
}

bool UParticleSystemComponent::IsPreviewLODActive() const
{
	if (PreviewLODOverride < 0)
		return false;
	const UWorld* World = GetWorld();
	return !(World && World->IsGameWorld());
}

int32 UParticleSystemComponent::ComputeDesiredLOD() const
{
	if (!Template)
		return -1;

	const UWorld* World = GetWorld();
	if (!World)
		return -1;

	// 여러 뷰 중 가장 가까운 카메라 기준 (가장 자세히 보이는 곳에 맞춘다)
	const TArray<FVector>& ViewLocations = World->GetViewLocationsRenderedLastFrame();
	if (ViewLocations.IsEmpty())
		return -1;

	const FVector Location = GetWorldLocation();
	float MinDistanceSq = FLT_MAX;
	for (const FVector& ViewLocation : ViewLocations)
	{
		const FVector Delta = ViewLocation - Location;
		const float DistanceSq = Delta.Dot(Delta);
		if (DistanceSq < MinDistanceSq)
		{
			MinDistanceSq = DistanceSq;
		}
	}

	// 제곱끼리 비교해 sqrt를 피한다
	const TArray<float>& Distances = Template->GetLODDistances();
	for (int32 i = Distances.Num() - 1; i > 0; --i)
	{
		if (MinDistanceSq >= Distances[i] * Distances[i])
			return i;
	}
	return 0;
}

void UParticleSystemComponent::UpdateLODLevel(float DeltaTime)
{
	if (!Template || IsPreviewLODActive())
		return;

	if (bPendingActivateLOD)
	{
		const int32 DesiredLOD = ComputeDesiredLOD();
		if (DesiredLOD >= 0)
		{
			ApplyLODLevel(DesiredLOD);
			bPendingActivateLOD = false;
			LODCheckTimer = 0.0f;
		}
		return;
	}

	// DirectSet은 코드가 정한 값, ActivateAutomatic은 활성화 때 정한 값을 유지한다
	if (Template->GetLODMethod() != EParticleSystemLODMethod::Automatic)
		return;

	LODCheckTimer += DeltaTime;
	if (LODCheckTimer < Template->GetLODDistanceCheckTime())
		return;
	LODCheckTimer = 0.0f;

	const int32 DesiredLOD = ComputeDesiredLOD();
	if (DesiredLOD >= 0 && DesiredLOD != LODLevel)
	{
		ApplyLODLevel(DesiredLOD);
	}
}

void UParticleSystemComponent::ApplyLODLevel(int32 InLODLevel)
{
	const int32 NumLODs = Template ? Template->GetNumLODs() : 1;
	LODLevel = (std::clamp)(InLODLevel, 0, (std::max)(NumLODs - 1, 0));

	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (Instance)
		{
			Instance->SetCurrentLODIndex(LODLevel);
		}
	}
}

FPrimitiveSceneProxy* UParticleSystemComponent::CreateSceneProxy()
{
	return new FParticleSystemSceneProxy(this);
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	FBox SimBounds(FVector(FLT_MAX), FVector(-FLT_MAX));
	bool bAny = false;
	bool bLocalSpace = false;
	// 위치만 담긴 박스라 스프라이트 크기만큼 기본 여유를 준다
	float PadSize = 50.0f;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		SimBounds.Expand(Instance->BoundingBox);
		bLocalSpace = Instance->IsLocalSpace();
		bAny = true;

		// 메시 파티클은 메시 반경 × 최대 크기만큼 튀어나온다
		const FParticleMeshEmitterInstance* MeshInstance = dynamic_cast<const FParticleMeshEmitterInstance*>(Instance);
		const UParticleModuleTypeDataMesh* TypeData = MeshInstance ? MeshInstance->GetMeshTypeData() : nullptr;
		const UStaticMesh* Mesh = TypeData ? TypeData->GetMesh() : nullptr;
		if (Mesh)
		{
			const FBox& MeshBounds = Mesh->GetRenderBounds();
			const FVector MaxCorner(
				std::max(std::abs(MeshBounds.Min.X), std::abs(MeshBounds.Max.X)),
				std::max(std::abs(MeshBounds.Min.Y), std::abs(MeshBounds.Max.Y)),
				std::max(std::abs(MeshBounds.Min.Z), std::abs(MeshBounds.Max.Z)));
			const float MeshRadius = MaxCorner.Length();

			float MaxParticleSize = 0.0f;
			ParticleHelper::ForEachParticle(*Instance, [&](FBaseParticle& Particle, uint8*)
				{
					MaxParticleSize = std::max(MaxParticleSize,
						std::max(std::abs(Particle.Size.X), std::max(std::abs(Particle.Size.Y), std::abs(Particle.Size.Z))));
				});
			PadSize = std::max(PadSize, MeshRadius * MaxParticleSize);
		}
	}
	if (!bAny)
		return FBox(FVector(-1.0f), FVector(1.0f));

	const FVector Pad(PadSize);
	SimBounds = FBox(SimBounds.Min - Pad, SimBounds.Max + Pad);

	// 프록시가 LocalToWorld를 다시 곱하므로 World Space면 로컬로 되돌린다
	return bLocalSpace ? SimBounds : SimBounds.GetWorldAABB(GetWorldMatrix().Inverse());
}

void DrawParticleSystems(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher)
		return;

	constexpr float HalfLength = 3.0f;
	const FVector4 Color(1.0f, 1.0f, 0.0f, 1.0f);

	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* PSC = *It;
		if (!PSC || !PSC->IsRegistered() || PSC->GetWorld() != World)
			continue;
		if (Selected && PSC->GetOwner() != Selected)
			continue;

		for (FParticleEmitterInstance* Instance : PSC->GetEmitterInstances())
		{
			if (!Instance || Instance->ActiveParticles == 0)
				continue;

			ParticleHelper::ForEachParticle(*Instance, [&](FBaseParticle& Particle, uint8* /*ParticleBase*/)
				{
						const FVector P = Instance->SimulationToWorld(Particle.Location);

					LineBatcher->AddLine(P - FVector(HalfLength, 0, 0), P + FVector(HalfLength, 0, 0), Color);
					LineBatcher->AddLine(P - FVector(0, HalfLength, 0), P + FVector(0, HalfLength, 0), Color);
					LineBatcher->AddLine(P - FVector(0, 0, HalfLength), P + FVector(0, 0, HalfLength), Color);
				});
		}
	}
}