#include "EnginePCH.h"
#include "ParticleLODLevel.h"

#include "ParticleModule.h"
#include "Module/ParticleModuleRequired.h"
#include "Module/ParticleModuleSpawn.h"
#include "Module/ParticleModuleTypeDataBase.h"

namespace
{
	UParticleModule* CreateModuleByClassName(UObject* Outer, const FString& ClassName)
	{
		UClass* Class = FindClass(ClassName);
		if (!Class || !Class->IsChildOf(UParticleModule::StaticClass()))
		{
			KRS_LOG(Warning, "Particle module class not found, dropped {}", ClassName);
			return nullptr;
		}

		if (Class->IsChildOf(UParticleModuleRequired::StaticClass()) ||
			Class->IsChildOf(UParticleModuleSpawn::StaticClass()) ||
			Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass()))
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

	for (UParticleModule* Module : Modules)
	{
		// 비활성 모듈도 목록에 둔다. bEnabled는 실행 루프에서 검사
		if (!Module)
			continue;

		if (Module->IsSpawnModule())  SpawnModules.Add(Module);
		if (Module->IsUpdateModule()) UpdateModules.Add(Module); 
	}
}

void UParticleLODLevel::AddModule(UParticleModule* InModule)
{
	if (!InModule || Modules.Contains(InModule))
		return;
	Modules.Add(InModule);
	UpdateModuleLists();
}

void UParticleLODLevel::RemoveModule(UParticleModule* InModule)
{
	if (!InModule)
		return;
	Modules.Remove(InModule);
	UpdateModuleLists();
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
