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
#include "Particles/Module/ParticleModuleTypeDataBeam.h"
#include "Particles/Module/ParticleModuleTypeDataRibbon.h"
#include "Particles/Module/ParticleModuleVelocity.h"
#include <algorithm>
#include <cmath>

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

	float SimDelta = bSimulationPaused ? 0.0f : DeltaTime * SimulationSpeed;
	if (PendingStepTime > 0.0f)
	{
		SimDelta = PendingStepTime;
		PendingStepTime = 0.0f;
	}

	if (SimDelta > 0.0f)
	{
		UpdateOrbitPreview(SimDelta);
		FAssetEditor::Tick(SimDelta);
	}
}

void FParticleSystemEditor::StepSimulation(float StepSeconds)
{
	bSimulationPaused = true;
	PendingStepTime = (std::max)(StepSeconds, 0.0f);
}

void FParticleSystemEditor::SetSimulationSpeed(float InSpeed)
{
	SimulationSpeed = std::clamp(InSpeed, 0.01f, 10.0f);
}

void FParticleSystemEditor::UpdateOrbitPreview(float DeltaTime)
{
	if (!bOrbitPreview || !PreviewComponent)
	{
		return;
	}

	OrbitAngle = std::fmod(OrbitAngle + OrbitSpeed * DeltaTime, 360.0f);
	const float Radians = OrbitAngle * PI / 180.0f;
	PreviewComponent->SetRelativeLocation(FVector(std::cos(Radians), std::sin(Radians), 0.0f) * OrbitRadius);
}

void FParticleSystemEditor::SetOrbitPreviewEnabled(bool bEnabled)
{
	if (bOrbitPreview == bEnabled)
	{
		return;
	}

	bOrbitPreview = bEnabled;
	OrbitAngle = 0.0f;
	if (PreviewComponent)
	{
		PreviewComponent->SetRelativeLocation(bEnabled ? FVector(OrbitRadius, 0.0f, 0.0f) : FVector::ZeroVector);
	}
	RestartSimulation();
}

bool FParticleSystemEditor::IsModuleClassCompatible(const UParticleModuleTypeDataBase* TypeData, const UClass* ModuleClass, const char** OutReason)
{
	if (!ModuleClass)
	{
		return true;
	}

	const UClass* TypeClass = TypeData ? TypeData->GetClass() : nullptr;
	auto Require = [&](const UClass* Needed, const char* Reason)
	{
		if (TypeClass && TypeClass->IsChildOf(Needed))
		{
			return true;
		}
		if (OutReason)
		{
			*OutReason = Reason;
		}
		return false;
	};

	switch (ModuleClass->GetDefaultObject<UParticleModule>()->GetModuleType())
	{
	case EPMT_Beam: return Require(UParticleModuleTypeDataBeam::StaticClass(), "requires Beam TypeData");
	case EPMT_Trail: return Require(UParticleModuleTypeDataRibbon::StaticClass(), "requires Ribbon TypeData");
	default: return true;
	}
}

bool FParticleSystemEditor::IsModuleCompatible(const UParticleEmitter* Emitter, const UParticleModule* Module, const char** OutReason) const
{
	const UParticleLODLevel* LODLevel = GetCurrentLODLevel(Emitter);
	if (!LODLevel || !Module)
	{
		return true;
	}
	return IsModuleClassCompatible(LODLevel->GetTypeDataModule(), Module->GetClass(), OutReason);
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

void FParticleSystemEditor::FocusPreview()
{
	if (!ViewportClient || !PreviewComponent)
	{
		return;
	}

	constexpr float MinFocusRadius = 100.0f;

	// 카메라는 월드 좌표로 맞추므로 월드 바운드를 쓴다 (오빗 중에는 컴포넌트가 원점에 있지 않다)
	FBox Box = PreviewComponent->CalcBounds();
	if (bOrbitPreview)
	{
		// 오빗 원 전체가 화면에 들어오도록 원의 바운드를 합친다
		const FVector OrbitCenter = PreviewComponent->GetWorldLocation() - PreviewComponent->GetRelativeLocation();
		const FVector OrbitExtent(OrbitRadius, OrbitRadius, 0.0f);
		Box.Expand(FBox(OrbitCenter - OrbitExtent, OrbitCenter + OrbitExtent));
	}
	const FVector Extent = (Box.Max - Box.Min) * 0.5f;
	FBoxSphereBounds Bound
	{
		.Origin = (Box.Min + Box.Max) * 0.5f,
		.BoxExtent = Extent,
		.SphereRadius = FMath::Max(Extent.Size(), MinFocusRadius),
	};

	ViewportClient->FocusOnBounds(Bound);
}

void FParticleSystemEditor::RestartSimulation()
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

namespace
{
	constexpr int32 SlotNone = -100;
	constexpr int32 SlotTypeData = -3;
	constexpr int32 SlotRequired = -2;
	constexpr int32 SlotSpawn = -1;

	int32 FindModuleSlot(const UParticleLODLevel* LODLevel, const UParticleModule* Module)
	{
		if (!LODLevel || !Module)
		{
			return SlotNone;
		}
		if (Module == LODLevel->GetTypeDataModule())
		{
			return SlotTypeData;
		}
		if (Module == LODLevel->GetRequiredModule())
		{
			return SlotRequired;
		}
		if (Module == LODLevel->GetSpawnModule())
		{
			return SlotSpawn;
		}
		const int32 Index = LODLevel->FindModuleIndex(Module);
		return Index >= 0 ? Index : SlotNone;
	}

	UParticleModule* GetModuleAtSlot(const UParticleLODLevel* LODLevel, int32 Slot)
	{
		if (!LODLevel)
		{
			return nullptr;
		}
		switch (Slot)
		{
		case SlotNone: return nullptr;
		case SlotTypeData: return LODLevel->GetTypeDataModule();
		case SlotRequired: return LODLevel->GetRequiredModule();
		case SlotSpawn: return LODLevel->GetSpawnModule();
		default:
			return LODLevel->GetModules().IsValidIndex(Slot) ? LODLevel->GetModules()[Slot] : nullptr;
		}
	}
}

int32 FParticleSystemEditor::GetNumLODs() const
{
	return WorkingCopy ? (std::max)(WorkingCopy->GetNumLODs(), 1) : 1;
}

UParticleLODLevel* FParticleSystemEditor::GetCurrentLODLevel(const UParticleEmitter* Emitter) const
{
	if (!Emitter || Emitter->GetNumLODLevels() == 0)
	{
		return nullptr;
	}
	return Emitter->GetLODLevel((std::min)(CurrentLOD, Emitter->GetNumLODLevels() - 1));
}

int32 FParticleSystemEditor::GetSelectedModuleSlot() const
{
	return FindModuleSlot(GetCurrentLODLevel(SelectedEmitter), SelectedModule);
}

void FParticleSystemEditor::ResolveSelectedModule(int32 Slot)
{
	SelectedModule = GetModuleAtSlot(GetCurrentLODLevel(SelectedEmitter), Slot);
}

void FParticleSystemEditor::ApplyPreviewLOD()
{
	if (PreviewComponent)
	{
		PreviewComponent->SetPreviewLODOverride(CurrentLOD);
	}
}

void FParticleSystemEditor::ClampCurrentLOD()
{
	const int32 Clamped = (std::clamp)(CurrentLOD, 0, GetNumLODs() - 1);
	if (Clamped != CurrentLOD)
	{
		SetCurrentLOD(Clamped);
	}
}

void FParticleSystemEditor::SetCurrentLOD(int32 Index)
{
	const int32 Slot = GetSelectedModuleSlot();
	CurrentLOD = (std::clamp)(Index, 0, GetNumLODs() - 1);
	ResolveSelectedModule(Slot);
	ApplyPreviewLOD();
}

bool FParticleSystemEditor::InsertLOD(int32 Index)
{
	const int32 Slot = GetSelectedModuleSlot();
	if (!WorkingCopy->InsertLOD(Index))
	{
		return false;
	}

	CurrentLOD = Index;
	ResolveSelectedModule(Slot);
	ApplyPreviewLOD();
	bDirty = true;
	return true;
}

bool FParticleSystemEditor::RemoveCurrentLOD()
{
	const int32 Slot = GetSelectedModuleSlot();
	SelectedModule = nullptr;
	if (!WorkingCopy->RemoveLOD(CurrentLOD))
	{
		ResolveSelectedModule(Slot);
		return false;
	}

	CurrentLOD = (std::min)(CurrentLOD, GetNumLODs() - 1);
	ResolveSelectedModule(Slot);
	ApplyPreviewLOD();
	bDirty = true;
	return true;
}

void FParticleSystemEditor::SetLODDistance(int32 Index, float Distance)
{
	WorkingCopy->SetLODDistance(Index, Distance);
	bDirty = true;
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

	for (int32 Index = 1; Index < GetNumLODs(); ++Index)
	{
		if (!Emitter->DuplicateLODLevel(Index - 1, Index))
		{
			KRS_LOG(Warning, "Particle LOD {} could not be added to the new emitter", Index);
			break;
		}
	}

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
	if (!Emitter || !ModuleClass || !GetCurrentLODLevel(Emitter))
	{
		return;
	}

	for (UParticleLODLevel* LODLevel : Emitter->GetLODLevels())
	{
		if (LODLevel)
		{
			LODLevel->AddModule(NewObject<UParticleModule>(LODLevel, ModuleClass));
		}
	}

	NotifyStructureChanged();

	const TArray<UParticleModule*>& Modules = GetCurrentLODLevel(Emitter)->GetModules();
	SelectModule(Emitter, Modules.IsEmpty() ? nullptr : Modules[Modules.Num() - 1]);
}

void FParticleSystemEditor::RemoveModule(const UParticleEmitter* Emitter, const UParticleModule* Module)
{
	const UParticleLODLevel* CurrentLODLevel = GetCurrentLODLevel(Emitter);
	const int32 ModuleIndex = CurrentLODLevel ? CurrentLODLevel->FindModuleIndex(Module) : -1;
	if (ModuleIndex < 0)
	{
		return;
	}

	TArray<UParticleModule*> Removed;
	for (UParticleLODLevel* LODLevel : Emitter->GetLODLevels())
	{
		if (!LODLevel || !LODLevel->GetModules().IsValidIndex(ModuleIndex))
		{
			continue;
		}
		UParticleModule* LODModule = LODLevel->GetModules()[ModuleIndex];
		if (LODLevel->RemoveModule(LODModule))
		{
			Removed.Add(LODModule);
		}
	}

	if (Removed.Contains(SelectedModule))
	{
		SelectedModule = nullptr;
	}

	NotifyStructureChanged();
	for (UParticleModule* LODModule : Removed)
	{
		delete LODModule;
	}
}

void FParticleSystemEditor::MoveModule(const UParticleEmitter* Emitter, int32 From, int32 To)
{
	if (!Emitter)
	{
		return;
	}

	bool bMoved = false;
	for (UParticleLODLevel* LODLevel : Emitter->GetLODLevels())
	{
		if (LODLevel && LODLevel->MoveModule(From, To))
		{
			bMoved = true;
		}
	}

	if (bMoved)
	{
		NotifyStructureChanged();
	}
}

void FParticleSystemEditor::SetTypeData(const UParticleEmitter* Emitter, UClass* TypeDataClass)
{
	if (!Emitter || !GetCurrentLODLevel(Emitter))
	{
		return;
	}

	// 모든 LOD가 이미 같은 종류면 다시 만들지 않는다 (기존 설정 유지)
	bool bAlreadySet = true;
	for (const UParticleLODLevel* LODLevel : Emitter->GetLODLevels())
	{
		if (!LODLevel)
		{
			continue;
		}
		const UParticleModuleTypeDataBase* Current = LODLevel->GetTypeDataModule();
		const UClass* CurrentClass = Current ? Current->GetClass() : nullptr;
		if (CurrentClass != TypeDataClass)
		{
			bAlreadySet = false;
			break;
		}
	}
	if (bAlreadySet)
	{
		return;
	}

	TArray<UParticleModule*> Removed;
	for (UParticleLODLevel* LODLevel : Emitter->GetLODLevels())
	{
		if (!LODLevel)
		{
			continue;
		}
		if (UParticleModule* Old = LODLevel->GetTypeDataModule())
		{
			Removed.Add(Old);
		}
		LODLevel->SetTypeDataModule(TypeDataClass ? NewObject<UParticleModuleTypeDataBase>(LODLevel, TypeDataClass) : nullptr);
	}

	if (Removed.Contains(SelectedModule))
	{
		SelectedModule = nullptr;
	}

	NotifyStructureChanged();
	for (UParticleModule* Old : Removed)
	{
		delete Old;
	}
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
	ClampCurrentLOD();
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
		// 파일은 썼지만 로드된 에셋이 옛 내용이다. Revert가 옛 내용으로 돌아가지 않도록 dirty를 유지한다
		KRS_LOG(Warning, "Saved, but failed to update loaded asset: {} ({})", GetAssetPath(), Error);
		return false;
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
	CurrentLOD = (std::min)(CurrentLOD, GetNumLODs() - 1);
	ApplyPreviewLOD();
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
	ApplyPreviewLOD();
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
	TUniquePtr<FParticleSystemEditorTab> NewTab = MakeUnique<FParticleSystemEditorTab>(*this);
	NewTab->SetBottomDrawerPanels(EditorEngine.GetContentDrawerPanel(), EditorEngine.GetOutputLogPanel());
	return NewTab;
}
