#pragma once

#include "ParticleSystemEditor.h"
#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FParticleSystemEditorTab : public FDockedAssetEditorTab
{
public:
	explicit FParticleSystemEditorTab(FParticleSystemEditor& InEditor);
	void OnRender(bool bActive) override;

protected:
	void BeforeRender() override { ParticleEditor.SetViewportState(0, 0, {}); }
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;
	bool DrawFileMenuItems() override;

	FParticleSystemEditor& ParticleEditor;
};
