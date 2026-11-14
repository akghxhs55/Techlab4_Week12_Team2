#include "EnginePCH.h"
#include "Emitter.h"

#include "ParticleEmitter.h"
#include "ParticleLODLevel.h"
#include "ParticleSpriteEmitter.h"
#include "ParticleSystem.h"
#include "ParticleSystemComponent.h"
#include "Asset/AssetManager.h"
#include "Components/BillboardComponent.h"

AEmitter::AEmitter()
{
	ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("ParticleSystemComponent");
	SetRootComponent(ParticleSystemComponent);

	UParticleSystem* System = NewObject<UParticleSystem>();
	UParticleSpriteEmitter* Emitter = NewObject<UParticleSpriteEmitter>(System);
	Emitter->CreateLODLevel(0);
	Emitter->SetToSensibleDefaults();
	System->Emitters.Add(Emitter);
	ParticleSystemComponent->SetTemplate(System);
	
	SpriteComponent = CreateDefaultSubobject<UBillboardComponent>("SpriteComponent");
	SpriteComponent->SetEditorIcon(true);
	SpriteComponent->SetIconSize(2.0f);
	SpriteComponent->SetHiddenInGame(true);
	SpriteComponent->SetHiddenInDetails(true);
	SpriteComponent->SetupAttachment(ParticleSystemComponent);

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Icon = UAssetManager::Get().LoadTexture("Engine/Icons/Emitter.png");
	if (Base && Icon)
	{
		UMaterial* IconMat = UMaterial::CreateInstance(Base);
		IconMat->Textures[0] = Icon;
		SpriteComponent->SetMaterial(0, IconMat);
	}
}
