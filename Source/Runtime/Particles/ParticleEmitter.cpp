#include "EnginePCH.h"
#include "ParticleEmitter.h"

#include "ParticleEmitterInstance.h"
#include "ParticleLODLevel.h"
#include "ParticleModuleRequired.h"
#include "ParticleModuleSpawn.h"
#include "Asset/AssetManager.h"

TUniquePtr<FParticleEmitterInstance> UParticleEmitter::CreateInstance(UParticleSystemComponent* Component)
{
	if (LODLevels.IsEmpty())
	{
		return nullptr;
	}

	TUniquePtr<FParticleEmitterInstance> Instance = MakeUnique<FParticleEmitterInstance>(*Component);
	Instance->InitParameters(this);
	Instance->CurrentLODLevelIndex = 0;
	Instance->CurrentLODLevel = LODLevels[0];
	Instance->Init();

	return Instance;
}

int32 UParticleEmitter::CreateLODLevel(int32 LODLevel)
{
	if (LODLevel != LODLevels.Num())
	{
		return -1;
	}

	UParticleLODLevel* Created = NewObject<UParticleLODLevel>(this);
	Created->Level = LODLevel;
	Created->bEnabled = true;
	Created->RequiredModule = NewObject<UParticleModuleRequired>(GetOuter());
	Created->SpawnModule = NewObject<UParticleModuleSpawn>(GetOuter());

	LODLevels.Add(Created);
	return LODLevel;
}

void UParticleEmitter::SetToSensibleDefaults()
{
	if (LODLevels.IsEmpty() || !LODLevels[0]->RequiredModule)
	{
		return;
	}

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Texture = UAssetManager::GetAssetByPath<UTexture2D>("Engine/Textures/SpotLightWarm.png");
	if (!Base)
	{
		return;
	}
	UMaterial* Material = UMaterial::CreateInstance(Base);
	if (Texture && !Material->Textures.IsEmpty())
	{
		Material->Textures[0] = Texture;
	}
	LODLevels[0]->RequiredModule->SetMaterial(Material);
}

const TArray<UParticleLODLevel*>& UParticleEmitter::GetLODLevels() const
{
	return LODLevels;
}
