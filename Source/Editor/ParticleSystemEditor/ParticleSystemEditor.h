#pragma once

#include "Camera/ViewportCameraMovement.h"
#include "Editor/AssetEditor/AssetEditor.h"

class UParticleSystemComponent;
class UParticleModule;
class UParticleEmitter;
class UParticleLODLevel;
class UParticleSystem;
class UParticleModuleTypeDataBase;
class UClass;

class FParticleSystemEditor : public FAssetEditor
{
public:
	FParticleSystemEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, 
		const FString& InAssetPath, UParticleSystem& InAsset);

	void Tick(float DeltaTime) override;
	void OnRenderPreview() override;
	const char* GetEditorTypeName() const override { return "ParticleSystem"; }

	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input);
	void FocusPreview();

	void RestartSimulation();

	bool IsSimulationPaused() const { return bSimulationPaused; }
	void SetSimulationPaused(bool bPaused) { bSimulationPaused = bPaused; }
	void ToggleSimulationPaused() { bSimulationPaused = !bSimulationPaused; }
	void StepSimulation(float StepSeconds = 1.0f / 60.0f);
	float GetSimulationSpeed() const { return SimulationSpeed; }
	void SetSimulationSpeed(float InSpeed);

	UObject* GetSelectedObject() const; // Emitter가 선택되어 있으면 Emitter를, Module이 선택되어 있으면 Module을 반환

	int32 GetNumLODs() const;
	int32 GetCurrentLOD() const { return CurrentLOD; }
	void SetCurrentLOD(int32 Index);
	UParticleLODLevel* GetCurrentLODLevel(const UParticleEmitter* Emitter) const;
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

	static bool IsModuleClassCompatible(const UParticleModuleTypeDataBase* TypeData, const UClass* ModuleClass, const char** OutReason = nullptr);
	bool IsModuleCompatible(const UParticleEmitter* Emitter, const UParticleModule* Module, const char** OutReason = nullptr) const;

	bool IsOrbitPreviewEnabled() const { return bOrbitPreview; }
	void SetOrbitPreviewEnabled(bool bEnabled);
	float GetOrbitRadius() const { return OrbitRadius; }
	void SetOrbitRadius(float InRadius) { OrbitRadius = (std::max)(InRadius, 0.0f); }
	float GetOrbitSpeed() const { return OrbitSpeed; }
	void SetOrbitSpeed(float InDegreesPerSecond) { OrbitSpeed = InDegreesPerSecond; }

	bool IsStatsOverlayEnabled() const { return bShowStats; }
	void SetStatsOverlayEnabled(bool bEnabled) { bShowStats = bEnabled; }
	UParticleSystemComponent* GetPreviewComponent() const { return PreviewComponent; }

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
	void ApplyPreviewLOD();
	void UpdateOrbitPreview(float DeltaTime);

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

	bool bOrbitPreview = false;
	float OrbitRadius = 100.0f;
	float OrbitSpeed = 180.0f;
	float OrbitAngle = 0.0f;

	bool bShowStats = false;

	bool bSimulationPaused = false;
	float SimulationSpeed = 1.0f;
	float PendingStepTime = 0.0f;

	bool bUnsavedPromptRequested = false;
	bool bCloseConfirmed = false;
};
