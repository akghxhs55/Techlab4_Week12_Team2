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
	void DrawToolbar() override;
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;
	bool DrawFileMenuItems() override;

private:
	void DrawSimulationControls() const;
	void DrawLODTabs(const UParticleSystem& System) const;
	void DrawLODDistance(const UParticleSystem& System) const;
	void DrawOrbitControls(float RightPadding) const;
	void DrawUnsavedChangesPrompt();

	FParticleSystemEditor& ParticleEditor;
};
