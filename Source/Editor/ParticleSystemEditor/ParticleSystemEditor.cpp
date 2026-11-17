#include "EnginePCH.h"
#include "ParticleSystemEditor.h"

#include "ParticleSystemEditorTab.h"
#include "Asset/AssetUtils.h"
#include "Components/ParticleSystemComponent.h"
#include "Core/Misc/Paths.h"
#include "Editor/KurisuEd/EditorEngine.h"
#include "Launch/LaunchEngineLoop.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemFile.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSpawn.h"
#include "Particles/Module/ParticleModuleColor.h"
#include "Particles/Module/ParticleModuleLifetime.h"
#include "Particles/Module/ParticleModuleSize.h"
#include "Particles/Module/ParticleModuleTypeDataBase.h"
#include "Particles/Module/ParticleModuleVelocity.h"

FParticleSystemEditor::FParticleSystemEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
                                             const FString& InAssetPath, UParticleSystem& InAsset)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
	, OriginalAsset(&InAsset)
{
}

void FParticleSystemEditor::Tick(float DeltaTime)
{
	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	if (bViewportVisible)
	{
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = true
		};
		ApplyViewportCameraMovement(Camera, CameraInput, DeltaTime, 5.0f, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
	}

	CameraInput = {};

	FAssetEditor::Tick(DeltaTime);
}

void FParticleSystemEditor::OnRenderPreview()
{
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender)
	{
		return;
	}

	if (!ViewportClient)
	{
		return;
	}

	if (!ViewportClient->GetWorld())
	{
		return;
	}

	ViewportClient->Resize(ViewportWidth, ViewportHeight);

	if (!ViewportClient->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
	{
		return;
	}

	if (bInitialFocusPending)
	{
		FocusPreview();
		bInitialFocusPending = false;
	}

	ViewportClient->Draw(EditorEngine.GetEngineLoop().GetRenderer());
}

void FParticleSystemEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
}

void FParticleSystemEditor::FocusPreview() const
{
	if (!ViewportClient || !PreviewComponent)
	{
		return;
	}

	constexpr float MinFocusRadius = 100.0f;

	const FBox& Box = PreviewComponent->CalcLocalBounds();
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = FMath::Max(Extent.Size(), MinFocusRadius),
	};

	ViewportClient->FocusOnBounds(Bound);
}

void FParticleSystemEditor::RestartSimulation() const
{
	if (PreviewComponent)
	{
		PreviewComponent->ActivateSystem(true);
	}
}

UObject* FParticleSystemEditor::GetSelectedObject() const
{
	if (SelectedModule)
	{
		return SelectedModule;
	}
	return SelectedEmitter;
}

void FParticleSystemEditor::SelectEmitter(UParticleEmitter* Emitter)
{
	SelectedEmitter = Emitter;
	SelectedModule = nullptr;
}

UParticleEmitter* FParticleSystemEditor::AddEmitter()
{
	UParticleEmitter* Emitter = NewObject<UParticleEmitter>(WorkingCopy);
	UParticleLODLevel* LODLevel = NewObject<UParticleLODLevel>(Emitter);
	LODLevel->SetLevel(0);
	LODLevel->SetRequiredModule(NewObject<UParticleModuleRequired>(LODLevel));
	LODLevel->SetSpawnModule(NewObject<UParticleModuleSpawn>(LODLevel));

	LODLevel->AddModule(NewObject<UParticleModuleLifetime>(LODLevel));
	LODLevel->AddModule(NewObject<UParticleModuleSize>(LODLevel));
	LODLevel->AddModule(NewObject<UParticleModuleVelocity>(LODLevel));
	LODLevel->AddModule(NewObject<UParticleModuleColor>(LODLevel));

	Emitter->AddLODLevel(LODLevel);
	WorkingCopy->AddEmitter(Emitter);

	NotifyStructureChanged();
	SelectEmitter(Emitter);
	return Emitter;
}

void FParticleSystemEditor::RemoveEmitter(UParticleEmitter* Emitter)
{
	if (!WorkingCopy->RemoveEmitter(Emitter))
	{
		return;
	}

	if (SelectedEmitter == Emitter)
	{
		SelectedEmitter = nullptr;
		SelectedModule = nullptr;
	}

	NotifyStructureChanged();
	delete Emitter;
}

void FParticleSystemEditor::MoveEmitter(int32 From, int32 To)
{
	WorkingCopy->MoveEmitter(From, To);
	NotifyStructureChanged();
}

void FParticleSystemEditor::SelectModule(UParticleEmitter* Emitter, UParticleModule* Module)
{
	SelectedEmitter = Emitter;
	SelectedModule = Module;
}

void FParticleSystemEditor::AddModule(UParticleEmitter* Emitter, UClass* ModuleClass)
{
	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (!LODLevel || !ModuleClass)
	{
		return;
	}

	UParticleModule* Module = NewObject<UParticleModule>(LODLevel, ModuleClass);
	LODLevel->AddModule(Module);
	NotifyStructureChanged();
	SelectModule(Emitter, Module);
}

void FParticleSystemEditor::RemoveModule(UParticleEmitter* Emitter, UParticleModule* Module)
{
	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (!LODLevel || !LODLevel->RemoveModule(Module))
	{
		return;
	}

	if (SelectedModule == Module)
	{
		SelectedModule = nullptr;
	}

	NotifyStructureChanged();
	delete Module;
}

void FParticleSystemEditor::MoveModule(UParticleEmitter* Emitter, int32 From, int32 To)
{
	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (LODLevel && LODLevel->MoveModule(From, To))
	{
		NotifyStructureChanged();
	}
}

void FParticleSystemEditor::SetTypeData(UParticleEmitter* Emitter, UClass* TypeDataClass)
{
	UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(0) : nullptr;
	if (!LODLevel)
	{
		return;
	}

	UParticleModuleTypeDataBase* Old = LODLevel->GetTypeDataModule();
	LODLevel->SetTypeDataModule(TypeDataClass ? NewObject<UParticleModuleTypeDataBase>(LODLevel, TypeDataClass) : nullptr);

	if (SelectedModule == Old)
	{
		SelectedModule = nullptr;
	}

	NotifyStructureChanged();
	delete Old;
}

void FParticleSystemEditor::NotifyPropertyEdited()
{
	bDirty = true;
}

void FParticleSystemEditor::NotifyStructureChanged()
{
	PreviewComponent->ResetParticles();
	WorkingCopy->BuildEmitters();
	PreviewComponent->ActivateSystem(true);
	bDirty = true;
}

bool FParticleSystemEditor::Save()
{
	FString Error;
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(GetAssetPath());
	if (!ParticleSystemFile::Write(FPaths::FromUtf8(FilePath), *WorkingCopy, Error))
	{
		KRS_LOG(Warning, "Failed to save particle system: {} ({})", GetAssetPath(), Error);
		return false;
	}
	if (!OriginalAsset->CopyContentsFrom(*WorkingCopy, Error))
	{
		KRS_LOG(Warning, "Saved, but failed to update loaded asset: {}", Error);
	}

	bDirty = false;
	return true;
}

bool FParticleSystemEditor::RevertToSaved()
{
	FString Error;
	UParticleSystem* Fresh = OriginalAsset->Duplicate(Error);
	if (!Fresh)
	{
		return false;
	}

	PreviewComponent->ResetParticles();
	SelectedEmitter = nullptr;
	SelectedModule = nullptr;
	delete WorkingCopy;
	WorkingCopy = Fresh;
	PreviewComponent->InitializeSystem(WorkingCopy);
	PreviewComponent->ActivateSystem(true);
	bDirty = false;
	return true;
}

bool FParticleSystemEditor::ConfirmClose()
{
	if (!IsDirty() || bCloseConfirmed)
	{
		return true;
	}

	bUnsavedPromptRequested = true;
	return false;
}

void FParticleSystemEditor::CloseWithoutPrompt()
{
	bCloseConfirmed = true;
	Close();
}

bool FParticleSystemEditor::InitializeResources()
{
	FString Error;
	WorkingCopy = OriginalAsset->Duplicate(Error);
	if (!WorkingCopy)
	{
		KRS_LOG(Warning, "Failed to duplicate particle system: {} ({})", GetAssetPath(), Error);
		return false;
	}

	if (!FAssetEditor::InitializeResources())
	{
		return false;
	}

	PreviewComponent = Cast<UParticleSystemComponent>(PreviewScene.AddComponent(UParticleSystemComponent::StaticClass()));
	if (!PreviewComponent)
	{
		return false;
	}

	PreviewComponent->SetTemplate(WorkingCopy);
	PreviewComponent->ActivateSystem(true);
	return true;
}

void FParticleSystemEditor::ShutdownResources()
{
	PreviewComponent = nullptr;
	FAssetEditor::ShutdownResources();
	delete WorkingCopy;
	WorkingCopy = nullptr;
	SelectedEmitter = nullptr;
	SelectedModule = nullptr;
}

TUniquePtr<IEditorTab> FParticleSystemEditor::CreateTab()
{
	return MakeUnique<FParticleSystemEditorTab>(*this);
}
