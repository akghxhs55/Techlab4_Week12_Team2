#include "EnginePCH.h"
#include "ParticleSystemEmitterPanel.h"

#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleSystem.h"
#include "Particles/Module/ParticleModuleRequired.h"
#include "Particles/Module/ParticleModuleSpawn.h"
#include "Particles/Module/ParticleModuleTypeDataBase.h"

namespace
{
	constexpr float ColumnWidth = 170.0f;
	constexpr const char* ModuleDragType = "PARTICLE_MODULE";
	constexpr const char* EmitterDragType = "PARTICLE_EMITTER";

	struct FModuleDragPayload
	{
		int32 EmitterIndex;
		int32 ModuleIndex;
	};

	ImU32 GetModuleColor(const UParticleModule* Module)
	{
		switch (Module->GetModuleType())
		{
		case EPMT_Required: return IM_COL32(200, 190, 60, 255);
		case EPMT_Spawn: return IM_COL32(200, 80, 60, 255);
		case EPMT_TypeData: return IM_COL32(60, 160, 200, 255);
		case EPMT_Beam: return IM_COL32(150, 90, 200, 255);
		case EPMT_Trail: return IM_COL32(60, 180, 120, 255);
		default: return IM_COL32(90, 90, 90, 255);
		}
	}

	FString StripPrefix(const FString& Name, const char* Prefix)
	{
		const size_t Len = strlen(Prefix);
		if (Name.compare(0, Len, Prefix) == 0 && Name.size() > Len)
		{
			return Name.substr(Len);
		}
		return Name;
	}

	FString GetModuleDisplayName(const UParticleModule* Module)
	{
		FString Name = Module->GetClass()->Name;
		for (const char* Prefix : { "UParticleModule", "ParticleModule" })
		{
			const FString Stripped = StripPrefix(Name, Prefix);
			if (Stripped.size() != Name.size())
			{
				Name = Stripped;
				break;
			}
		}

		// TypeData 모듈은 에미터 종류 이름(Mesh, Beam, Ribbon ...)으로 표시. Base는 스프라이트
		if (Module->GetModuleType() == EPMT_TypeData)
		{
			const FString TypeName = StripPrefix(Name, "TypeData");
			return TypeName == "Base" ? FString("Sprite") : TypeName;
		}
		return Name;
	}

	// 오른쪽 끝에 Enabled 체크박스를 둘 때 이름 Selectable이 쓸 수 있는 폭
	float GetWidthBeforeRightCheckbox()
	{
		return (std::max)(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x, 1.0f);
	}
}

FParticleSystemEmitterPanel::FParticleSystemEmitterPanel(FParticleSystemEditor& InEditor)
	: FParticleSystemEditorPanel(InEditor, "Emitter")
{
}

void FParticleSystemEmitterPanel::DrawContent()
{
	UParticleSystem* System = Editor.GetWorkingCopy();
	if (!System)
	{
		return;
	}

	PendingAction = nullptr;

	ImGui::BeginChild("##EmitterStrip", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);

	for (int32 EmitterIndex = 0; EmitterIndex < System->GetNumEmitters(); ++EmitterIndex)
	{
		if (EmitterIndex > 0)
		{
			ImGui::SameLine();
		}
		DrawEmitterColumn(EmitterIndex, System->GetEmitter(EmitterIndex));
	}

	if (ImGui::BeginPopupContextWindow("##StripContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		if (ImGui::MenuItem("Add New Particle Sprite Emitter"))
		{
			PendingAction = [this] { Editor.AddEmitter(); };
		}
		ImGui::EndPopup();
	}

	ImGui::EndChild();

	HandleShortcuts();

	if (PendingAction)
	{
		PendingAction();
		PendingAction = nullptr;
	}
}

void FParticleSystemEmitterPanel::DrawEmitterColumn(int32 EmitterIndex, UParticleEmitter* Emitter)
{
	UParticleLODLevel* LODLevel = Editor.GetCurrentLODLevel(Emitter);
	if (!LODLevel)
	{
		return;
	}

	ImGui::PushID(EmitterIndex);
	ImGui::BeginChild("##Column", ImVec2(ColumnWidth, 0), ImGuiChildFlags_Borders);

	DrawEmitterHeader(EmitterIndex, Emitter, LODLevel);
	ImGui::Separator();

	if (LODLevel->GetTypeDataModule())
	{
		DrawModuleRow(Emitter, LODLevel->GetTypeDataModule(), EmitterIndex, -1);
	}
	DrawModuleRow(Emitter, LODLevel->GetRequiredModule(), EmitterIndex, -1);
	DrawModuleRow(Emitter, LODLevel->GetSpawnModule(), EmitterIndex, -1);
	ImGui::Separator();

	const TArray<UParticleModule*>& Modules = LODLevel->GetModules();
	for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
	{
		DrawModuleRow(Emitter, Modules[ModuleIndex], EmitterIndex, ModuleIndex);
	}

	if (ImGui::BeginPopupContextWindow("##ColumnContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
	{
		DrawAddModuleMenu(Emitter);
		ImGui::EndPopup();
	}

	ImGui::EndChild();
	ImGui::PopID();
}

void FParticleSystemEmitterPanel::DrawEmitterHeader(int32 EmitterIndex, UParticleEmitter* Emitter, UParticleLODLevel* LODLevel)
{
	char Label[32];
	snprintf(Label, sizeof(Label), "Emitter %d", EmitterIndex);
	const bool bSelected = Editor.GetSelectedEmitter() == Emitter && !Editor.IsModuleSelected();
	ImGui::AlignTextToFramePadding();
	if (ImGui::Selectable(Label, bSelected, ImGuiSelectableFlags_None, ImVec2(GetWidthBeforeRightCheckbox(), 0)))
	{
		Editor.SelectEmitter(Emitter);
	}

	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EmitterDragType, &EmitterIndex, sizeof(int32));
		ImGui::TextUnformatted(Label);
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EmitterDragType))
		{
			const int32 From = *static_cast<const int32*>(Payload->Data);
			if (From != EmitterIndex)
			{
				PendingAction = [this, From, To = EmitterIndex] { Editor.MoveEmitter(From, To); };
			}
		}
		ImGui::EndDragDropTarget();
	}

	// 드래그/드롭이 Selectable에 붙도록 체크박스는 그 뒤에 그린다
	ImGui::SameLine();
	bool bEnabled = LODLevel->IsEnabled();
	if (ImGui::Checkbox("##Enabled", &bEnabled))
	{
		LODLevel->SetEnabled(bEnabled);
		Editor.NotifyPropertyEdited();
	}

	if (ImGui::BeginPopupContextWindow("##EmitterMenu"))
	{
		if (ImGui::BeginMenu("TypeData"))
		{
			for (UClass* Class : GetTypeDataClasses())
			{
				if (ImGui::MenuItem(Class->Name.c_str()))
				{
					PendingAction = [this, Emitter, Class] { Editor.SetTypeData(Emitter, Class); };
				}
			}

			if (LODLevel->GetTypeDataModule() && ImGui::MenuItem("Remove TypeData"))
			{
				PendingAction = [this, Emitter] { Editor.SetTypeData(Emitter, nullptr); };
			}

			ImGui::EndMenu();
		}

		ImGui::Separator();
		
		if (ImGui::MenuItem("Delete Emitter"))
		{
			PendingAction = [this, Emitter] { Editor.RemoveEmitter(Emitter); };
		}

		ImGui::EndPopup();
	}
}

void FParticleSystemEmitterPanel::DrawModuleRow(UParticleEmitter* Emitter, UParticleModule* Module, int32 EmitterIndex, int32 ModuleIndex)
{
	if (!Module)
	{
		return;
	}

	const bool bListModule = ModuleIndex >= 0;
	ImGui::PushID(Module);

	const char* InvalidReason = nullptr;
	const bool bCompatible = Editor.IsModuleCompatible(Emitter, Module, &InvalidReason);
	constexpr ImU32 InvalidColor = IM_COL32(230, 70, 60, 255);

	const ImVec2 Pos = ImGui::GetCursorScreenPos();
	const float RowHeight = ImGui::GetFrameHeight();
	ImGui::GetWindowDrawList()->AddRectFilled(Pos, ImVec2(Pos.x + 4.0f, Pos.y + RowHeight),
		bCompatible ? GetModuleColor(Module) : InvalidColor);
	ImGui::SetCursorScreenPos(ImVec2(Pos.x + 8.0f, Pos.y));
	ImGui::AlignTextToFramePadding();

	const FString Name = GetModuleDisplayName(Module);
	const bool bDimmed = !Module->IsEnabled();
	if (!bCompatible)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, InvalidColor);
	}
	else if (bDimmed)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
	}

	const float NameWidth = bListModule ? GetWidthBeforeRightCheckbox() : 0.0f;
	if (ImGui::Selectable(Name.c_str(), Editor.GetSelectedModule() == Module, ImGuiSelectableFlags_None, ImVec2(NameWidth, 0)))
	{
		Editor.SelectModule(Emitter, Module);
	}
	if (!bCompatible && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s %s - this module has no effect", Name.c_str(), InvalidReason);
	}

	if (!bCompatible || bDimmed)
	{
		ImGui::PopStyleColor();
	}

	if (bListModule)
	{
		if (ImGui::BeginDragDropSource())
		{
			const FModuleDragPayload Payload{ EmitterIndex, ModuleIndex };
			ImGui::SetDragDropPayload(ModuleDragType, &Payload, sizeof(Payload));
			ImGui::TextUnformatted(Name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* P = ImGui::AcceptDragDropPayload(ModuleDragType))
			{
				const auto& Drag = *static_cast<const FModuleDragPayload*>(P->Data);
				if (Drag.EmitterIndex == EmitterIndex && Drag.ModuleIndex != ModuleIndex)
				{
					PendingAction = [this, Emitter, From = Drag.ModuleIndex, To = ModuleIndex] { Editor.MoveModule(Emitter, From, To); };
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::BeginPopupContextItem("##ModuleMenu"))
		{
			if (ImGui::MenuItem("Delete Module", "Del"))
			{
				PendingAction = [this, Emitter, Module] { Editor.RemoveModule(Emitter, Module); };
			}
			ImGui::EndPopup();
		}

		// 드래그/드롭과 컨텍스트 메뉴가 Selectable에 붙도록 체크박스는 마지막에 그린다
		ImGui::SameLine();
		bool bEnabled = Module->IsEnabled();
		if (ImGui::Checkbox("##On", &bEnabled))
		{
			Module->SetEnabled(bEnabled);
			Editor.NotifyPropertyEdited();
		}
	}

	ImGui::PopID();
}

void FParticleSystemEmitterPanel::DrawAddModuleMenu(UParticleEmitter* Emitter)
{
	const UParticleLODLevel* LODLevel = Editor.GetCurrentLODLevel(Emitter);
	const UParticleModuleTypeDataBase* TypeData = LODLevel ? LODLevel->GetTypeDataModule() : nullptr;

	for (UClass* Class : GetAddableModuleClasses())
	{
		const char* Reason = nullptr;
		const bool bCompatible = FParticleSystemEditor::IsModuleClassCompatible(TypeData, Class, &Reason);
		if (ImGui::MenuItem(Class->Name.c_str(), nullptr, false, bCompatible))
		{
			PendingAction = [this, Emitter, Class] { Editor.AddModule(Emitter, Class); };
		}
		if (!bCompatible && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		{
			ImGui::SetTooltip("%s", Reason);
		}
	}
}

void FParticleSystemEmitterPanel::HandleShortcuts()
{
	if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
	{
		return;
	}

	if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
	{
		UParticleEmitter* Emitter = Editor.GetSelectedEmitter();
		if (UParticleModule* Module = Editor.GetSelectedModule())
		{
			PendingAction = [this, Emitter, Module] { Editor.RemoveModule(Emitter, Module); };
		}
		else if (Emitter)
		{
			PendingAction = [this, Emitter] { Editor.RemoveEmitter(Emitter); };
		}
	}
}

const TArray<UClass*>& FParticleSystemEmitterPanel::GetAddableModuleClasses()
{
	static TArray<UClass*> Result;
	if (Result.IsEmpty())
	{
		TArray<UClass*> All;
		GetDerivedClasses(UParticleModule::StaticClass(), All);
		for (UClass* Class : All)
		{
			if (!Class->Constructor)
			{
				continue;
			}

			const EModuleType Type = Class->GetDefaultObject<UParticleModule>()->GetModuleType();
			if (Type == EPMT_Required || Type == EPMT_Spawn || Type == EPMT_TypeData)
			{
				continue;
			}

			Result.Add(Class);
		}
	}

	return Result;
}

const TArray<UClass*>& FParticleSystemEmitterPanel::GetTypeDataClasses()
{
	static TArray<UClass*> Result;
	if (Result.IsEmpty())
	{
		TArray<UClass*> All;
		GetDerivedClasses(UParticleModule::StaticClass(), All);
		for (UClass* Class : All)
		{
			if (!Class->Constructor || !Class->IsChildOf(UParticleModuleTypeDataBase::StaticClass()))
			{
				continue;
			}

			Result.Add(Class);
		}
	}

	return Result;
}
