#pragma once

#include "ParticleSystemEditorPanel.h"

class FParticleSystemCurveEditorPanel : public FParticleSystemEditorPanel
{
public:
	explicit FParticleSystemCurveEditorPanel(FParticleSystemEditor& InEditor);

protected:
	void DrawContent() override;
};
