#pragma once

#include "Camera/ViewportCameraMovement.h"
#include "Editor/AssetEditor/AssetEditor.h"

class UParticleSystemComponent;
class UParticleModule;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleSystem;

class FParticleSystemEditor : public FAssetEditor
{
public:
	FParticleSystemEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, 
		const FString& InAssetPath, UParticleSystem& InAsset);

	void Tick(float DeltaTime) override;
	void OnRenderPreview() override;
	const char* GetEditorTypeName() const override { return "ParticleSystem"; }

	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input);
	void FocusPreview() const;

	void RestartSimulation() const;

	UObject* GetSelectedObject() const; // Emitter가 선택되어 있으면 Emitter를, Module이 선택되어 있으면 Module을 반환

	int32 GetNumLODs() const;
	int32 GetCurrentLOD() const { return CurrentLOD; }
	void SetCurrentLOD(int32 Index);
	UParticleLODLevel* GetCurrentLODLevel(const UParticleEmitter* Emitter) const;
	// Index 위치에 Index - 1 LOD의 복사본을 넣고 선택한다 (Index >= 1)
	bool InsertLOD(int32 Index);
	bool RemoveCurrentLOD();
	void SetLODDistance(int32 Index, float Distance);

	void SelectEmitter(UParticleEmitter* Emitter);
	UParticleEmitter* GetSelectedEmitter() const { return SelectedEmitter; }
	UParticleEmitter* AddEmitter();
	void RemoveEmitter(UParticleEmitter* Emitter);
	void MoveEmitter(int32 From, int32 To);

	void SelectModule(UParticleEmitter* Emitter, UParticleModule* Module);
	bool IsModuleSelected() const { return SelectedModule != nullptr; }
	UParticleModule* GetSelectedModule() const { return SelectedModule; }
	void AddModule(UParticleEmitter* Emitter, UClass* ModuleClass);
	void RemoveModule(const UParticleEmitter* Emitter, const UParticleModule* Module);
	void MoveModule(const UParticleEmitter* Emitter, int32 From, int32 To);
	void SetTypeData(const UParticleEmitter* Emitter, UClass* TypeDataClass);

	void NotifyPropertyEdited();
	void NotifyStructureChanged();

	bool IsDirty() const override { return bDirty; }
	bool Save();
	bool RevertToSaved();
	bool ConfirmClose() override;
	void CloseWithoutPrompt();
	bool IsUnsavedPromptRequested() const { return bUnsavedPromptRequested; }
	void ClearUnsavedPromptRequest() { bUnsavedPromptRequested = false; }

	UParticleSystem* GetWorkingCopy() const { return WorkingCopy; }

protected:
	bool InitializeResources() override;
	void ShutdownResources() override;
	TUniquePtr<IEditorTab> CreateTab() override;

private:
	int32 GetSelectedModuleSlot() const;
	void ResolveSelectedModule(int32 Slot);
	void ClampCurrentLOD();
	void ApplyPreviewLOD() const;

	UParticleSystem* OriginalAsset = nullptr;
	UParticleSystem* WorkingCopy = nullptr;
	UParticleSystemComponent* PreviewComponent = nullptr;

	UParticleEmitter* SelectedEmitter = nullptr;
	UParticleModule* SelectedModule = nullptr;
	int32 CurrentLOD = 0;

	bool bDirty = false;

	FViewportCameraInput CameraInput;
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;

	bool bUnsavedPromptRequested = false;
	bool bCloseConfirmed = false;
};
