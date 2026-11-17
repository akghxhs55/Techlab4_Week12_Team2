#pragma once

#include "ParticleSystemEditorPanel.h"

class FParticleSystemDetailsPanel : public FParticleSystemEditorPanel
{
public:
	explicit FParticleSystemDetailsPanel(FParticleSystemEditor& InEditor);

protected:
	void DrawContent() override;
};
