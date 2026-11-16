#include "EnginePCH.h"
#include "ParticleSystem.h"
#include "ParticleEmitter.h"
#include "ParticleSystemFile.h"
#include "ParticleEmitterInstance.h"
#include "Components/ParticleSystemComponent.h"
#include "UObject/UObjectIterator.h"

void UParticleSystem::BuildEmitters()
{
	for (UParticleEmitter* Emitter : Emitters)
	{
		if (Emitter)
			Emitter->Build();
	}
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

	// 1. 이 에셋을 Template으로 쓰는 컴포넌트 수집 (PIE 포함)
	struct FUser
	{
		UParticleSystemComponent* Component;
		bool bHadInstances;
		bool bWasActive;
	};
	TArray<FUser> Users;
	for (TObjectIterator<UParticleSystemComponent> It; It; ++It)
	{
		UParticleSystemComponent* Component = *It;
		if (Component->GetTemplate() != this)
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

	// 2. 인스턴스가 원본의 Emitter/LODLevel/Module을 가리키고 있으므로 먼저 해제
	for (const FUser& User : Users)
	{
		User.Component->ResetParticles();
	}

	// 3. 원본 비우기, 4. 새 내용 로드
	ClearEmitters();
	if (!ParticleSystemFile::LoadFromBytes(Bytes, *this, OutError))
	{
		// 검증을 통과했으므로 여기 오면 안 된다. 반쯤 채워진 상태로 두지 않는다
		ClearEmitters();
	}

	// 5. 실행 중이던 컴포넌트만 다시 초기화
	for (const FUser& User : Users)
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
