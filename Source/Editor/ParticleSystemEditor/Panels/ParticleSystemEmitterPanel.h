#pragma once

#include "ParticleSystemEditorPanel.h"

class UParticleLODLevel;

class FParticleSystemEmitterPanel : public FParticleSystemEditorPanel
{
public:
	explicit FParticleSystemEmitterPanel(FParticleSystemEditor& InEditor);

protected:
	void DrawContent() override;

private:
	void DrawEmitterColumn(int32 EmitterIndex, UParticleEmitter* Emitter);
	void DrawEmitterHeader(int32 EmitterIndex, UParticleEmitter* Emitter, UParticleLODLevel* LODLevel);
	void DrawModuleRow(UParticleEmitter* Emitter, UParticleModule* Module, int32 EmitterIndex, int32 ModuleIndex);
	void DrawAddModuleMenu(UParticleEmitter* Emitter);
	void HandleShortcuts();

	std::function<void()> PendingAction;

	static const TArray<UClass*>& GetAddableModuleClasses();
	static const TArray<UClass*>& GetTypeDataClasses();
};
