#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/ParticleSystemEditor/ParticleSystemEditor.h"

class FParticleSystemEditorPanel : public TAssetEditorPanel<FParticleSystemEditor>
{
public:
	FParticleSystemEditorPanel(FParticleSystemEditor& InEditor, const char* InName)
		: TAssetEditorPanel(InEditor, InName, "ParticlePanel")
	{}
};
