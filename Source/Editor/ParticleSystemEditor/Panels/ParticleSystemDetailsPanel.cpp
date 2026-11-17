#include "EnginePCH.h"
#include "ParticleSystemDetailsPanel.h"

#include "Editor/Details/PropertyEditor.h"

FParticleSystemDetailsPanel::FParticleSystemDetailsPanel(FParticleSystemEditor& InEditor)
	: FParticleSystemEditorPanel(InEditor, "Details")
{
}

void FParticleSystemDetailsPanel::DrawContent()
{
	UObject* Target = Editor.GetSelectedObject();
	if (!Target)
	{
		ImGui::TextDisabled("Select an emitter or module.");
		return;
	}
	if (FPropertyEditor::DrawObjectProperties(Target))
	{
		Editor.NotifyPropertyEdited();
	}
}
