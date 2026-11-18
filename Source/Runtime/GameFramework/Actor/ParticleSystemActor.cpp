#include "EnginePCH.h"
#include "ParticleSystemActor.h"
#include "Asset/AssetManager.h"

AParticleSystemActor::AParticleSystemActor()
{
	ParticleSystemComponent = CreateDefaultSubobject<UParticleSystemComponent>("UParticleSystemComponent");
	SetRootComponent(ParticleSystemComponent);

	UParticleSystem* DefaultTemplate = UAssetManager::LoadParticleSystem("Particles/DefaultTest.particle");
	ParticleSystemComponent->InitializeSystem(DefaultTemplate);

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetIconSize(2.0f);
	BillboardComponent->SetHiddenInGame(true);
	BillboardComponent->SetupAttachment(ParticleSystemComponent);
	BillboardComponent->SetHiddenInDetails(true);

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Icon = UAssetManager::Get().LoadTexture("Engine/Icons/Emitter.png");
	if (Base && Icon)
	{
		UMaterial* IconMat = UMaterial::CreateInstance(Base);
		IconMat->Textures[0] = Icon;
		BillboardComponent->SetMaterial(0, IconMat);
	}
}
