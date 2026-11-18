#include "EnginePCH.h"
#include "ParticleSystemDetailsPanel.h"

#include "Editor/Details/PropertyEditor.h"
#include "Editor/ParticleSystemEditor/DistributionCustomization.h"
#include "Particles/ParticleModule.h"

FParticleSystemDetailsPanel::FParticleSystemDetailsPanel(FParticleSystemEditor& InEditor)
	: FParticleSystemEditorPanel(InEditor, "Details")
{
	RegisterDistributionCustomizations();
}

void FParticleSystemDetailsPanel::DrawContent()
{
	UObject* Target = Editor.GetSelectedObject();
	if (!Target)
	{
		ImGui::TextDisabled("Select an emitter or module.");
		return;
	}
	UParticleModule* Module = Cast<UParticleModule>(Target);

	const char* Reason = nullptr;
	if (Module && !Editor.IsModuleCompatible(Editor.GetSelectedEmitter(), Module, &Reason))
	{
		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 80, 70, 255));
		ImGui::TextWrapped("This module has no effect: %s", Reason);
		ImGui::PopStyleColor();
		ImGui::Separator();
	}

	const uint32 BytesBefore = Module ? Module->RequiredBytes() : 0;
	if (FPropertyEditor::DrawObjectProperties(Target))
	{
		if (Module && Module->RequiredBytes() != BytesBefore)
		{
			Editor.NotifyStructureChanged();
		}
		else
		{
			Editor.NotifyPropertyEdited();
		}
	}
}
