#include "EnginePCH.h"
#include "ParticleEmitter.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "ParticleTypes.h"
#include "ParticleEmitterInstance.h"
#include "Module/ParticleModuleRequired.h"
#include "Module/ParticleModuleSpawn.h"

FParticleEmitterInstance* UParticleEmitter::CreateInstance(UParticleSystemComponent* InComponent)
{
	FParticleEmitterInstance* Instance = new FParticleEmitterInstance();
	Instance->Init(this, InComponent);
	return Instance;
}

void UParticleEmitter::AddLODLevel(UParticleLODLevel* InLODLevel)
{
	if (InLODLevel && !LODLevels.Contains(InLODLevel))
	{
		LODLevels.Add(InLODLevel);
	}
}

UParticleEmitter::~UParticleEmitter()
{
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		delete LODLevel;
	}
	LODLevels.Reset();
}

void UParticleEmitter::Build()
{
	UParticleLODLevel* LODLevel = GetLODLevel(0);
	if (!LODLevel) return;

	// 분포 굽기
	if (UParticleModule* RequiredModule = LODLevel->GetRequiredModule())
	{
		RequiredModule->BakeDistributions();
	}
	if (UParticleModule* SpawnModule = LODLevel->GetSpawnModule())
	{
		SpawnModule->BakeDistributions();
	}
	for (UParticleModule* Module : LODLevel->GetModules())
	{
		if (Module)
		{
			Module->BakeDistributions();
		}
	}
	// 스폰/업데이트 모듈 정리
	LODLevel->UpdateModuleLists();
	// 레이아웃 계산
	CacheEmitterModuleInfo();
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
	ModuleOffsetMap.Empty();
	ParticleSize = sizeof(FBaseParticle);            // 공통 데이터부터 시작

	UParticleLODLevel* LODLevel = GetLODLevel(0);
	if (!LODLevel)
		return;

	for (UParticleModule* Module : LODLevel->GetModules())
	{
		if (!Module)
			continue;
		const int32 ReqBytes = static_cast<int32>(Module->RequiredBytes());
		if (ReqBytes > 0)
		{
			ModuleOffsetMap.Add(Module, ParticleSize);   // 이 모듈의 Payload는 여기서 시작
			ParticleSize += ReqBytes;                    // 다음 모듈은 그 뒤에
		}
	}
	// 16바이트 정렬은 에미터 인스턴스가 이 값을 받아서 맞춘다
}

int32 UParticleEmitter::GetModuleOffset(UParticleModule* Module) const
{
	const int32* Found = ModuleOffsetMap.Find(Module);
	return Found ? *Found : 0;
}

void UParticleEmitter::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	int32 Count = Ar.IsSaving() ? LODLevels.Num() : 0;
	auto LODArray = Record.EnterArray("LODLevels", Count);

	if (Ar.IsLoading())
	{
		assert(LODLevels.IsEmpty());
		LODLevels.SetNum(Count);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		if (Ar.IsLoading())
		{
			LODLevels[i] = NewObject<UParticleLODLevel>(this);
			LODLevels[i]->SetLevel(i);
		}

		LODLevels[i]->Serialize(LODArray.EnterElement().EnterRecord());

		if (Ar.IsError())
		{
			break;
		}
	}
}
