#include "EnginePCH.h"
#include "ParticleLODLevel.h"

#include "ParticleModule.h"
#include "Module/ParticleModuleRequired.h"
#include "Module/ParticleModuleSpawn.h"
#include "Module/ParticleModuleTypeDataBase.h"
#include "Module/ParticleModuleEventGenerator.h"
#include "Module/ParticleModuleEventReceiverBase.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

namespace
{
	bool IsDedicatedSlotModuleClass(const UClass* Class)
	{
		return Class->IsChildOf(UParticleModuleRequired::StaticClass()) ||
			Class->IsChildOf(UParticleModuleSpawn::StaticClass()) ||
			Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass());
	}

	UParticleModule* CreateModuleByClassName(UObject* Outer, const FString& ClassName)
	{
		UClass* Class = FindClass(ClassName);
		if (!Class || !Class->IsChildOf(UParticleModule::StaticClass()))
		{
			KRS_LOG(Warning, "Particle module class not found, dropped {}", ClassName);
			return nullptr;
		}

		if (IsDedicatedSlotModuleClass(Class))
		{
			KRS_LOG(Warning, "Particle module '{}' cannot be in the module list, dropped", ClassName);
			return nullptr;
		}

		return NewObject<UParticleModule>(Outer, Class);
	}
}

UParticleLODLevel::~UParticleLODLevel()
{
	delete RequiredModule;
	RequiredModule = nullptr;

	delete SpawnModule;
	SpawnModule = nullptr;

	delete TypeDataModule;
	TypeDataModule = nullptr;
	
	for (UParticleModule* Module : Modules)
	{
		delete Module;
	}
	Modules.Reset();
}

void UParticleLODLevel::UpdateModuleLists()
{
	SpawnModules.Empty();
	UpdateModules.Empty();
	EventGenerator = nullptr;
	EventReceivers.Empty();

	for (UParticleModule* Module : Modules)
	{
		// 비활성 모듈도 목록에 둔다. bEnabled는 실행 루프에서 검사
		if (!Module)
			continue;

		if (!EventGenerator)
		{
			EventGenerator = Cast<UParticleModuleEventGenerator>(Module);
		}
		if (UParticleModuleEventReceiverBase* Receiver = Cast<UParticleModuleEventReceiverBase>(Module))
		{
			EventReceivers.Add(Receiver);
		}

		if (Module->IsSpawnModule())  SpawnModules.Add(Module);
		if (Module->IsUpdateModule()) UpdateModules.Add(Module); 
	}
}

UParticleLODLevel* UParticleLODLevel::Duplicate(UObject* Outer)
{
	// 에셋 저장과 같은 경로를 쓰므로 모듈 종류가 늘어나도 복사 코드를 고칠 필요가 없다
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Archive(Formatter);
		Serialize(Archive.Open().EnterRecord());
	}
	if (Writer.IsError())
	{
		KRS_LOG(Warning, "Particle LOD {} cannot be serialized for copy", Level);
		return nullptr;
	}

	UParticleLODLevel* Copy = NewObject<UParticleLODLevel>(Outer);
	FMemoryReader Reader(Bytes, true);
	FJsonArchiveInputFormatter Formatter(Reader);
	if (!Reader.IsError())
	{
		FStructuredArchive Archive(Formatter);
		Copy->Serialize(Archive.Open().EnterRecord());
	}
	if (Reader.IsError())
	{
		KRS_LOG(Warning, "Particle LOD {} copy cannot be loaded", Level);
		delete Copy;
		return nullptr;
	}

	Copy->SetLevel(Level);
	return Copy;
}

void UParticleLODLevel::AddModule(UParticleModule* InModule)
{
	if (!InModule || Modules.Contains(InModule))
		return;
	Modules.Add(InModule);
	UpdateModuleLists();
}

int32 UParticleLODLevel::FindModuleIndex(const UParticleModule* InModule) const
{
	for (int32 i = 0; i < Modules.Num(); ++i)
	{
		if (Modules[i] == InModule)
			return i;
	}
	return -1;
}

bool UParticleLODLevel::InsertModule(UParticleModule* InModule, int32 Index)
{
	if (!InModule || Modules.Contains(InModule) || IsDedicatedSlotModuleClass(InModule->GetClass()))
		return false;

	Index = (std::clamp)(Index, 0, Modules.Num());
	Modules.Insert(InModule, static_cast<uint32>(Index));
	UpdateModuleLists();
	return true;
}

bool UParticleLODLevel::RemoveModule(const UParticleModule* InModule)
{
	const int32 Index = FindModuleIndex(InModule);
	if (Index < 0)
		return false;

	Modules.RemoveAt(static_cast<uint32>(Index));
	UpdateModuleLists();
	return true;
}

bool UParticleLODLevel::MoveModule(int32 From, int32 To)
{
	if (!Modules.IsValidIndex(From) || !Modules.IsValidIndex(To))
		return false;
	if (From != To)
	{
		UParticleModule* Module = Modules[From];
		Modules.RemoveAt(static_cast<uint32>(From));
		Modules.Insert(Module, static_cast<uint32>(To));
		UpdateModuleLists();
	}
	return true;
}

void UParticleLODLevel::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);

	FArchive& Ar = Record.GetUnderlyingArchive();
	const bool bLoading = Ar.IsLoading();

	if (bLoading)
	{
		assert(!RequiredModule && !SpawnModule && Modules.IsEmpty());
		RequiredModule = NewObject<UParticleModuleRequired>(this);
		SpawnModule = NewObject<UParticleModuleSpawn>(this);
	}
	assert(RequiredModule && SpawnModule);

	RequiredModule->Serialize(Record.EnterRecord("RequiredModule"));
	SpawnModule->Serialize(Record.EnterRecord("SpawnModule"));
	if (Ar.IsError())
	{
		return;
	}

	TOptional<FStructuredArchive::FSlot> TypeDataSlot = Record.TryEnterField("TypeDataModule", TypeDataModule != nullptr);
	if (TypeDataSlot.IsSet())
	{
		FStructuredArchive::FRecord TypeDataRecord = TypeDataSlot.GetValue().EnterRecord();

		FString ClassName = bLoading ? FString{} : TypeDataModule->GetClass()->Name;
		TypeDataRecord << SA_VALUE("Class", ClassName);

		if (bLoading)
		{
			assert(!TypeDataModule);
			UClass* Class = FindClass(ClassName);
			if (Class && Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass()))
			{
				TypeDataModule = NewObject<UParticleModuleTypeDataBase>(this, Class);
			}
			else
			{
				KRS_LOG(Warning, "TypeData class not found, dropped {}", ClassName);
			}
		}

		if (TypeDataModule)
		{
			TypeDataModule->Serialize(TypeDataRecord.EnterRecord("Data"));
		}

		if (Ar.IsError())
		{
			return;
		}
	}

	int32 Count = bLoading ? 0 : Modules.Num();
	auto ModuleArray = Record.EnterArray("Modules", Count);

	for (int32 i = 0; i < Count; ++i)
	{
		auto ModuleRecord = ModuleArray.EnterElement().EnterRecord();

		FString ClassName = bLoading ? FString{} : Modules[i]->GetClass()->Name;
		ModuleRecord << SA_VALUE("Class", ClassName);

		UParticleModule* Module = bLoading ? CreateModuleByClassName(this, ClassName) : Modules[i];
		if (!Module)
		{
			continue;
		}

		Module->Serialize(ModuleRecord.EnterRecord("Data"));

		if (bLoading)
		{
			Modules.Add(Module);
		}

		if (Ar.IsError())
		{
			break;
		}
	}

	if (bLoading)
	{
		UpdateModuleLists();
	}
}
