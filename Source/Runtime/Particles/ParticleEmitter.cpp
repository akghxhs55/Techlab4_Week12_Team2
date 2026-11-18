#include "EnginePCH.h"
#include "ParticleEmitter.h"
#include "ParticleLODLevel.h"
#include "ParticleModule.h"
#include "ParticleTypes.h"
#include "ParticleEmitterInstance.h"
#include "Module/ParticleModuleRequired.h"
#include "Module/ParticleModuleSpawn.h"
#include "Module/ParticleModuleTypeDataBase.h"
#include "Module/ParticleModuleEventGenerator.h"
#include "Module/ParticleModuleEventReceiverBase.h"

FParticleEmitterInstance* UParticleEmitter::CreateInstance(UParticleSystemComponent* InComponent)
{
	// 인스턴스 종류(Payload 레이아웃)는 에미터 일생 동안 하나여야 하므로 LOD 0 기준
	UParticleLODLevel* LODLevel = GetLODLevel(0);
	UParticleModuleTypeDataBase* TypeData = LODLevel ? LODLevel->GetTypeDataModule() : nullptr;
	if (TypeData)
	{
		// TypeData가 생성과 Init까지 끝낸다
		if (FParticleEmitterInstance* TypedInstance = TypeData->CreateInstance(this, InComponent))
			return TypedInstance;
	}

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

UParticleLODLevel* UParticleEmitter::DuplicateLODLevel(int32 SourceIndex, int32 InsertIndex)
{
	UParticleLODLevel* Source = GetLODLevel(SourceIndex);
	if (!Source || InsertIndex < 0 || InsertIndex > LODLevels.Num())
		return nullptr;

	UParticleLODLevel* Copy = Source->Duplicate(this);
	if (!Copy)
		return nullptr;

	LODLevels.Insert(Copy, static_cast<uint32>(InsertIndex));
	RenumberLODLevels();
	return Copy;
}

bool UParticleEmitter::RemoveLODLevel(int32 Index)
{
	if (!LODLevels.IsValidIndex(Index) || LODLevels.Num() <= 1)
		return false;

	delete LODLevels[Index];
	LODLevels.RemoveAt(static_cast<uint32>(Index));
	RenumberLODLevels();
	return true;
}

void UParticleEmitter::RenumberLODLevels()
{
	for (int32 i = 0; i < LODLevels.Num(); ++i)
	{
		if (LODLevels[i])
		{
			LODLevels[i]->SetLevel(i);
		}
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

namespace
{
	void BakeLODLevelDistributions(UParticleLODLevel& LODLevel)
	{
		if (UParticleModule* RequiredModule = LODLevel.GetRequiredModule())
		{
			RequiredModule->BakeDistributions();
		}
		if (UParticleModule* SpawnModule = LODLevel.GetSpawnModule())
		{
			SpawnModule->BakeDistributions();
		}
		if (UParticleModuleTypeDataBase* TypeDataModule = LODLevel.GetTypeDataModule())
		{
			TypeDataModule->BakeDistributions();
		}
		for (UParticleModule* Module : LODLevel.GetModules())
		{
			if (Module)
			{
				Module->BakeDistributions();
			}
		}
	}

	// 같은 인덱스의 모듈끼리 같은 Payload 칸을 써도 되는지
	bool IsSameModuleSlot(const UParticleModule* HighModule, const UParticleModule* LODModule)
	{
		return HighModule && LODModule &&
			HighModule->GetClass() == LODModule->GetClass() &&
			HighModule->RequiredBytes() == LODModule->RequiredBytes();
	}
}

void UParticleEmitter::Build()
{
	// 어느 LOD로 전환해도 바로 실행할 수 있도록 모든 레벨을 준비한다
	for (UParticleLODLevel* LODLevel : LODLevels)
	{
		if (!LODLevel)
			continue;
		BakeLODLevelDistributions(*LODLevel);
		LODLevel->UpdateModuleLists();
	}

	ValidateLODLevels();
	CacheEmitterModuleInfo();
}

void UParticleEmitter::ValidateLODLevels() const
{
	const UParticleLODLevel* HighLOD = GetLODLevel(0);
	if (!HighLOD)
		return;

	const TArray<UParticleModule*>& HighModules = HighLOD->GetModules();
	const UParticleModuleRequired* HighRequired = HighLOD->GetRequiredModule();
	const UParticleModuleSpawn* HighSpawn = HighLOD->GetSpawnModule();
	const UParticleModuleTypeDataBase* HighTypeData = HighLOD->GetTypeDataModule();

	for (int32 LODIndex = 1; LODIndex < LODLevels.Num(); ++LODIndex)
	{
		const UParticleLODLevel* LODLevel = LODLevels[LODIndex];
		if (!LODLevel)
			continue;

		// 모듈 구성이 다르면 살아 있는 파티클의 Payload를 다른 칸으로 읽게 된다
		const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
		if (Modules.Num() != HighModules.Num())
		{
			KRS_LOG(Warning, "Particle LOD {} has {} modules, LOD 0 has {}", LODIndex, Modules.Num(), HighModules.Num());
		}
		const int32 CommonCount = Modules.Num() < HighModules.Num() ? Modules.Num() : HighModules.Num();
		for (int32 i = 0; i < CommonCount; ++i)
		{
			if (!IsSameModuleSlot(HighModules[i], Modules[i]))
			{
				KRS_LOG(Warning, "Particle LOD {} module {} does not match LOD 0", LODIndex, i);
				continue;
			}

			// 이벤트 이름·종류가 LOD마다 다르면 전환 순간 Receiver가 이벤트를 놓친다. 양(Frequency·SpawnCount)만 바꾼다
			if (const auto* HighGen = Cast<UParticleModuleEventGenerator>(HighModules[i]))
			{
				const auto* Gen = Cast<UParticleModuleEventGenerator>(Modules[i]);
				const auto& HighEvents = HighGen->GetEvents();
				const auto& Events = Gen->GetEvents();
				bool bSame = HighEvents.Num() == Events.Num();
				for (int32 e = 0; bSame && e < Events.Num(); ++e)
				{
					bSame = HighEvents[e].Type == Events[e].Type && HighEvents[e].CustomName == Events[e].CustomName;
				}
				if (!bSame)
				{
					KRS_LOG(Warning, "Particle LOD {} event generator {} differs from LOD 0 (types/names must match)", LODIndex, i);
				}
			}
			else if (const auto* HighRecv = Cast<UParticleModuleEventReceiverBase>(HighModules[i]))
			{
				const auto* Recv = Cast<UParticleModuleEventReceiverBase>(Modules[i]);
				if (HighRecv->GetEventGeneratorType() != Recv->GetEventGeneratorType() || HighRecv->GetEventName() != Recv->GetEventName())
				{
					KRS_LOG(Warning, "Particle LOD {} event receiver {} filter differs from LOD 0", LODIndex, i);
				}
			}
		}

		// 인스턴스 종류는 LOD 0 기준으로 한 번만 정해진다
		const UParticleModuleTypeDataBase* TypeData = LODLevel->GetTypeDataModule();
		const UClass* HighTypeClass = HighTypeData ? HighTypeData->GetClass() : nullptr;
		const UClass* TypeClass = TypeData ? TypeData->GetClass() : nullptr;
		if (HighTypeClass != TypeClass)
		{
			KRS_LOG(Warning, "Particle LOD {} TypeData differs from LOD 0", LODIndex);
		}

		// 좌표계가 바뀌면 살아 있는 파티클의 위치 해석이 달라진다
		const UParticleModuleRequired* Required = LODLevel->GetRequiredModule();
		if (HighRequired && Required && HighRequired->IsUseLocalSpace() != Required->IsUseLocalSpace())
		{
			KRS_LOG(Warning, "Particle LOD {} local space setting differs from LOD 0", LODIndex);
		}

		// Burst 발사 기록은 항목 번호로 남으므로 항목 수와 시간이 같아야 한다
		const UParticleModuleSpawn* Spawn = LODLevel->GetSpawnModule();
		if (HighSpawn && Spawn)
		{
			const TArray<FParticleBurst>& HighBursts = HighSpawn->GetBurstList();
			const TArray<FParticleBurst>& Bursts = Spawn->GetBurstList();
			if (HighBursts.Num() != Bursts.Num())
			{
				KRS_LOG(Warning, "Particle LOD {} has {} bursts, LOD 0 has {}", LODIndex, Bursts.Num(), HighBursts.Num());
			}
			else
			{
				for (int32 i = 0; i < Bursts.Num(); ++i)
				{
					if (std::fabs(HighBursts[i].Time - Bursts[i].Time) > 1.0e-4f)
					{
						KRS_LOG(Warning, "Particle LOD {} burst {} time differs from LOD 0", LODIndex, i);
					}
				}
			}
		}
	}
}

void UParticleEmitter::CacheEmitterModuleInfo()
{
	ModuleOffsetMap.Empty();
	ParticleSize = sizeof(FBaseParticle);            // 공통 데이터부터 시작

	UParticleLODLevel* LODLevel = GetLODLevel(0);
	if (!LODLevel)
		return;

	const TArray<UParticleModule*>& HighModules = LODLevel->GetModules();
	for (UParticleModule* Module : HighModules)
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

	// 다른 LOD의 모듈은 같은 인덱스의 LOD 0 모듈과 같은 칸을 쓴다.
	// 짝이 맞지 않는 모듈은 등록하지 않는다 (잘못된 칸에 쓰는 것보다 오프셋 0 assert가 낫다)
	for (int32 LODIndex = 1; LODIndex < LODLevels.Num(); ++LODIndex)
	{
		UParticleLODLevel* LowLODLevel = LODLevels[LODIndex];
		if (!LowLODLevel)
			continue;

		const TArray<UParticleModule*>& Modules = LowLODLevel->GetModules();
		const int32 CommonCount = Modules.Num() < HighModules.Num() ? Modules.Num() : HighModules.Num();
		for (int32 i = 0; i < CommonCount; ++i)
		{
			UParticleModule* HighModule = HighModules[i];
			UParticleModule* Module = Modules[i];
			if (Module == HighModule || !IsSameModuleSlot(HighModule, Module))
				continue;

			if (const int32* Offset = ModuleOffsetMap.Find(HighModule))
			{
				ModuleOffsetMap.Add(Module, *Offset);
			}
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
