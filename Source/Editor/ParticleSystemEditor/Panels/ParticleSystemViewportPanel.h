#pragma once

#include "ParticleSystemEditorPanel.h"

class FParticleSystemViewportPanel : public FParticleSystemEditorPanel
{
public:
	explicit FParticleSystemViewportPanel(FParticleSystemEditor& InEditor);

protected:
	void DrawContent() override;
	bool IsViewportPanel() const override { return true; }

private:
	void DrawStatsOverlay(const ImVec2& ViewMin) const;
};
