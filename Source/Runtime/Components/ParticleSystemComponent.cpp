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
	SendRenderDynamicData();
}

void UParticleSystemComponent::SendRenderDynamicData()
{
	FParticleSystemSceneProxy* Proxy = static_cast<FParticleSystemSceneProxy*>(GetSceneProxy());
	if (!Proxy)
		return;

	TArray<TUniquePtr<FDynamicEmitterDataBase>>& RenderData = Proxy->GetDynamicEmitterData();
	if (RenderData.Num() != EmitterInstances.Num())
	{
		RenderData.Empty();
		RenderData.SetNum(EmitterInstances.Num());
	}
	for (int32 Index = 0; Index < EmitterInstances.Num(); ++Index)
	{
		if (FParticleEmitterInstance* Instance = EmitterInstances[Index])
			Instance->UpdateDynamicData(RenderData[Index]);
		else
			RenderData[Index].reset();
	}
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
	SendRenderDynamicData();
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

namespace
{
	// 여러 뷰 중 가장 가까운 카메라까지의 제곱거리 (가장 자세히 보이는 곳에 맞춘다). 뷰가 없으면 -1
	float NearestViewDistanceSq(const UParticleSystemComponent& Component)
	{
		const UWorld* World = Component.GetWorld();
		if (!World)
			return -1.0f;

		const TArray<FVector>& ViewLocations = World->GetViewLocationsRenderedLastFrame();
		if (ViewLocations.IsEmpty())
			return -1.0f;

		const FVector Location = Component.GetWorldLocation();
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
		return MinDistanceSq;
	}
}

bool UParticleSystemComponent::bShowAllLODDistances = false;

float UParticleSystemComponent::GetDistanceToNearestView() const
{
	const float DistanceSq = NearestViewDistanceSq(*this);
	return DistanceSq < 0.0f ? -1.0f : std::sqrt(DistanceSq);
}

int32 UParticleSystemComponent::ComputeDesiredLOD() const
{
	if (!Template)
		return -1;

	const float MinDistanceSq = NearestViewDistanceSq(*this);
	if (MinDistanceSq < 0.0f)
		return -1;

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
	const int32 OldLODLevel = LODLevel;
	LODLevel = (std::clamp)(InLODLevel, 0, (std::max)(NumLODs - 1, 0));

	if (LODLevel != OldLODLevel)
	{
		int32 ActiveParticles = 0;
		for (const FParticleEmitterInstance* Instance : EmitterInstances)
		{
			ActiveParticles += Instance ? Instance->ActiveParticles : 0;
		}
		KRS_LOG(Info, "Particle LOD {} -> {} ({}, active particles {})", OldLODLevel, LODLevel,
			Template ? Template->GetAssetPath() : FString("None"), ActiveParticles);
	}

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

FBox UParticleSystemComponent::CalcBounds() const
{
	// 에미터가 자기 공간(Local/World)에 맞춰 월드 박스를 만들어 두므로 합치기만 한다
	FBox Bounds(FVector(FLT_MAX), FVector(-FLT_MAX));
	bool bAny = false;
	for (FParticleEmitterInstance* Instance : EmitterInstances)
	{
		if (!Instance || Instance->ActiveParticles == 0)
			continue;
		Bounds.Expand(Instance->BoundingBox);
		bAny = true;
	}
	if (!bAny)
	{
		const FVector Location = GetWorldLocation();
		return FBox(Location - FVector(1.0f), Location + FVector(1.0f));
	}
	return Bounds;
}

FBox UParticleSystemComponent::CalcLocalBounds() const
{
	// 로컬 박스를 요구하는 곳(디버그 박스)용. 회전이 있으면 실제보다 커진다
	return CalcBounds().GetWorldAABB(GetWorldMatrix().Inverse());
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

namespace
{
	// 위도 원과 경도 원으로 지구본처럼 그린다. 큰 구도 둥글게 보이도록 선을 촘촘히 둔다
	void AddWireGlobe(FLineBatcher& LineBatcher, const FVector& Center, float Radius, const FVector4& Color)
	{
		constexpr int32 LatitudeRings = 12;     // 극점 제외 위도 원 11개
		constexpr int32 MeridianCircles = 8;    // 경도 원 8개 = 경선 16개
		constexpr int32 Segments = 64;

		auto CirclePoint = [](float Angle) { return FVector(std::cos(Angle), std::sin(Angle), 0.0f); };

		for (int32 Ring = 1; Ring < LatitudeRings; ++Ring)
		{
			const float Latitude = PI * static_cast<float>(Ring) / LatitudeRings - PI * 0.5f;
			const float RingRadius = Radius * std::cos(Latitude);
			const FVector RingCenter = Center + FVector(0.0f, 0.0f, Radius * std::sin(Latitude));
			for (int32 i = 0; i < Segments; ++i)
			{
				const FVector A = CirclePoint(2.0f * PI * i / Segments) * RingRadius;
				const FVector B = CirclePoint(2.0f * PI * (i + 1) / Segments) * RingRadius;
				LineBatcher.AddLine(RingCenter + A, RingCenter + B, Color);
			}
		}

		for (int32 Meridian = 0; Meridian < MeridianCircles; ++Meridian)
		{
			const float Longitude = PI * static_cast<float>(Meridian) / MeridianCircles;
			const FVector Horizontal(std::cos(Longitude), std::sin(Longitude), 0.0f);
			for (int32 i = 0; i < Segments; ++i)
			{
				const float AngleA = 2.0f * PI * i / Segments;
				const float AngleB = 2.0f * PI * (i + 1) / Segments;
				const FVector A = Horizontal * (Radius * std::cos(AngleA)) + FVector(0.0f, 0.0f, Radius * std::sin(AngleA));
				const FVector B = Horizontal * (Radius * std::cos(AngleB)) + FVector(0.0f, 0.0f, Radius * std::sin(AngleB));
				LineBatcher.AddLine(Center + A, Center + B, Color);
			}
		}
	}
}

void DrawParticleLODDistances(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	const bool bAll = UParticleSystemComponent::bShowAllLODDistances;
	if (!World || !LineBatcher || (!Selected && !bAll))
		return;

	const FVector4 Color(0.2f, 1.0f, 0.3f, 1.0f);

	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* PSC = *It;
		if (!PSC || !PSC->IsRegistered() || PSC->GetWorld() != World)
			continue;
		if (!bAll && PSC->GetOwner() != Selected)
			continue;

		const UParticleSystem* Template = PSC->GetTemplate();
		if (!Template)
			continue;

		const FVector Center = PSC->GetWorldLocation();
		const TArray<float>& Distances = Template->GetLODDistances();
		if (Distances.Num() < 2)
			continue;

		// 경계 i는 LOD i-1과 LOD i 사이. 여러 개를 겹쳐 그리면 어지러우므로
		// 지금 LOD에서 다음 LOD로 넘어가는 바깥쪽 경계 하나만 그린다 (마지막 LOD면 안쪽 경계)
		const int32 CurrentLOD = PSC->GetLODLevel();
		const int32 Boundary = (std::min)(CurrentLOD + 1, Distances.Num() - 1);
		AddWireGlobe(*LineBatcher, Center, Distances[Boundary], Color);
	}
}