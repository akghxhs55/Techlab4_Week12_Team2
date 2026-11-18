#include "EnginePCH.h"
#include "ParticleSystem.h"
#include "ParticleEmitter.h"
#include "ParticleSystemFile.h"
#include "ParticleEmitterInstance.h"
#include "Components/ParticleSystemComponent.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// 새 LOD를 맨 뒤에 붙일 때 이전 거리 대비 간격
	constexpr float DefaultLODDistanceStep = 1000.0f;

	// 이 에셋을 Template으로 쓰는 컴포넌트 (PIE 포함)
	struct FParticleSystemUser
	{
		UParticleSystemComponent* Component;
		bool bHadInstances;
		bool bWasActive;
	};

	// 인스턴스가 Emitter/LODLevel/Module을 가리키고 있으므로 구조를 바꾸기 전에 해제한다
	TArray<FParticleSystemUser> ResetUsers(const UParticleSystem* System)
	{
		TArray<FParticleSystemUser> Users;
		for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
		{
			UParticleSystemComponent* Component = *It;
			if (Component->GetTemplate() != System)
				continue;

			bool bWasActive = false;
			for (const FParticleEmitterInstance* Instance : Component->GetEmitterInstances())
			{
				if (Instance && Instance->bSpawningEnabled)
				{
					bWasActive = true;
					break;
				}
			}
			Users.Add({ Component, !Component->GetEmitterInstances().IsEmpty(), bWasActive });
		}

		for (const FParticleSystemUser& User : Users)
		{
			User.Component->ResetParticles();
		}
		return Users;
	}

	// 실행 중이던 컴포넌트만 다시 초기화
	void RestoreUsers(const TArray<FParticleSystemUser>& Users)
	{
		for (const FParticleSystemUser& User : Users)
		{
			if (!User.bHadInstances)
				continue;

			User.Component->InitializeSystem();
			if (User.bWasActive)
			{
				User.Component->ActivateSystem();
			}
			else
			{
				User.Component->DeactivateSystem();
			}
		}
	}
}

void UParticleSystem::BuildEmitters()
{
	FixupLODSettings();
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
			Emitter->Build();
	}
}

void UParticleSystem::FixupLODSettings()
{
	int32 MinLODLevels = 0;
	int32 MaxLODLevels = 0;
	for (const UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter)
			continue;
		const int32 Count = Emitter->GetNumLODLevels();
		MinLODLevels = MaxLODLevels == 0 ? Count : (std::min)(MinLODLevels, Count);
		MaxLODLevels = (std::max)(MaxLODLevels, Count);
	}
	if (MinLODLevels != MaxLODLevels)
	{
		KRS_LOG(Warning, "Particle system emitters have different LOD counts ({} ~ {})", MinLODLevels, MaxLODLevels);
	}

	// LOD 데이터가 없던 에셋은 에미터 LOD 수만큼 채운다
	const int32 NumLODs = (std::max)(MaxLODLevels, 1);
	while (LODDistances.Num() < NumLODs)
	{
		const float Prev = LODDistances.IsEmpty() ? 0.0f : LODDistances[LODDistances.Num() - 1];
		LODDistances.Add(LODDistances.IsEmpty() ? 0.0f : Prev + DefaultLODDistanceStep);
	}
	while (LODDistances.Num() > NumLODs)
	{
		LODDistances.RemoveAt(static_cast<uint32>(LODDistances.Num() - 1));
	}
	LODDistances[0] = 0.0f;

	for (int32 i = 1; i < LODDistances.Num(); ++i)
	{
		if (LODDistances[i] <= LODDistances[i - 1])
		{
			KRS_LOG(Warning, "Particle LOD {} distance {} is not greater than LOD {}", i, LODDistances[i], i - 1);
		}
	}
}

void UParticleSystem::SetLODDistance(int32 Index, float Distance)
{
	if (Index <= 0 || !LODDistances.IsValidIndex(Index))
		return;
	LODDistances[Index] = Distance;
}

bool UParticleSystem::InsertLOD(int32 Index)
{
	const int32 NumLODs = GetNumLODs();
	if (Index < 1 || Index > NumLODs)
		return false;

	const TArray<FParticleSystemUser> Users = ResetUsers(this);

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (!Emitter || Emitter->GetNumLODLevels() == 0)
			continue;
		// LOD 수가 모자란 에미터는 자기 마지막 레벨을 복사해 개수를 맞춰 간다
		const int32 InsertIndex = (std::min)(Index, Emitter->GetNumLODLevels());
		if (!Emitter->DuplicateLODLevel(InsertIndex - 1, InsertIndex))
		{
			KRS_LOG(Warning, "Particle LOD {} could not be added to an emitter", Index);
		}
	}

	// 사이에 넣으면 양옆의 중간, 맨 뒤면 이전 거리 + 간격
	const float Prev = LODDistances[Index - 1];
	const float NewDistance = Index < NumLODs ? (Prev + LODDistances[Index]) * 0.5f : Prev + DefaultLODDistanceStep;
	LODDistances.Insert(NewDistance, static_cast<uint32>(Index));

	BuildEmitters();
	RestoreUsers(Users);
	return true;
}

bool UParticleSystem::RemoveLOD(int32 Index)
{
	if (Index < 1 || Index >= GetNumLODs())
		return false;

	const TArray<FParticleSystemUser> Users = ResetUsers(this);

	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter && Index < Emitter->GetNumLODLevels())
		{
			Emitter->RemoveLODLevel(Index);
		}
	}
	LODDistances.RemoveAt(static_cast<uint32>(Index));

	BuildEmitters();
	RestoreUsers(Users);
	return true;
}

UParticleSystem::~UParticleSystem()
{
	ClearEmitters();
}

void UParticleSystem::ClearEmitters()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		delete Emitter;
	}
	Emitters.Reset();
}

void UParticleSystem::AddEmitter(UParticleEmitter* InEmitter)
{
	if (InEmitter && !Emitters.Contains(InEmitter))
		Emitters.Add(InEmitter);
}

int32 UParticleSystem::FindEmitterIndex(const UParticleEmitter* InEmitter) const
{
	for (int32 i = 0; i < Emitters.Num(); ++i)
	{
		if (Emitters[i] == InEmitter)
			return i;
	}
	return -1;
}

bool UParticleSystem::InsertEmitter(UParticleEmitter* InEmitter, int32 Index)
{
	if (!InEmitter || Emitters.Contains(InEmitter))
		return false;

	Index = (std::clamp)(Index, 0, Emitters.Num());
	Emitters.Insert(InEmitter, static_cast<uint32>(Index));
	return true;
}

UParticleEmitter* UParticleSystem::RemoveEmitter(UParticleEmitter* InEmitter)
{
	const int32 Index = FindEmitterIndex(InEmitter);
	if (Index < 0)
		return nullptr;

	Emitters.RemoveAt(static_cast<uint32>(Index));
	return InEmitter;
}

bool UParticleSystem::MoveEmitter(int32 From, int32 To)
{
	if (!Emitters.IsValidIndex(From) || !Emitters.IsValidIndex(To))
		return false;
	if (From == To)
		return true;

	UParticleEmitter* Emitter = Emitters[From];
	Emitters.RemoveAt(static_cast<uint32>(From));
	Emitters.Insert(Emitter, static_cast<uint32>(To));
	return true;
}

UParticleSystem* UParticleSystem::Duplicate(FString& OutError)
{
	TArray<uint8> Bytes;
	if (!ParticleSystemFile::SaveToBytes(*this, Bytes, OutError))
		return nullptr;

	UParticleSystem* Copy = NewObject<UParticleSystem>();
	if (!ParticleSystemFile::LoadFromBytes(Bytes, *Copy, OutError))
	{
		delete Copy;
		return nullptr;
	}
	return Copy;
}

bool UParticleSystem::CopyContentsFrom(UParticleSystem& Source, FString& OutError)
{
	if (&Source == this)
		return true;

	TArray<uint8> Bytes;
	if (!ParticleSystemFile::SaveToBytes(Source, Bytes, OutError))
		return false;

	// 원본을 비우기 전에 bytes가 정상적으로 로드되는지 먼저 확인
	{
		UParticleSystem* Validate = NewObject<UParticleSystem>();
		const bool bValid = ParticleSystemFile::LoadFromBytes(Bytes, *Validate, OutError);
		delete Validate;
		if (!bValid)
			return false;
	}

	// 1. 이 에셋을 쓰는 컴포넌트를 모아 인스턴스 해제
	const TArray<FParticleSystemUser> Users = ResetUsers(this);

	// 2. 원본 비우기, 3. 새 내용 로드
	ClearEmitters();
	// Properties에 없는 필드는 기존 값이 남으므로 기본값으로 되돌린다
	LODDistances.Empty();
	LODMethod = EParticleSystemLODMethod::Automatic;
	LODDistanceCheckTime = 0.25f;
	if (!ParticleSystemFile::LoadFromBytes(Bytes, *this, OutError))
	{
		// 검증을 통과했으므로 여기 오면 안 된다. 반쯤 채워진 상태로 두지 않는다
		ClearEmitters();
	}

	// 4. 실행 중이던 컴포넌트만 다시 초기화
	RestoreUsers(Users);

	return OutError.empty();
}

void UParticleSystem::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();

	int32 Version = Ar.IsSaving() ? 1 : 0;
	Record << SA_VALUE("Version", Version);
	if (Version != 1)
	{
		Ar.SetError();
		return;
	}

	int32 Count = Ar.IsSaving() ? Emitters.Num() : 0;
	auto EmitterArray = Record.EnterArray("Emitters", Count);

	if (Ar.IsLoading())
	{
		assert(Emitters.IsEmpty());
		Emitters.SetNum(Count);
	}

	for (int32 i = 0; i < Count; ++i)
	{
		if (Ar.IsLoading())
		{
			Emitters[i] = NewObject<UParticleEmitter>(this);
		}

		Emitters[i]->Serialize(EmitterArray.EnterElement().EnterRecord());

		if (Ar.IsError())
		{
			break;
		}
	}
}
