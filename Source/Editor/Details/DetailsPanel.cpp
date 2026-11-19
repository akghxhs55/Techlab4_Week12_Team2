#include "EnginePCH.h"
#include "Editor/KurisuEd/EditorHierarchy.h"
#include "Editor/EditorUI/EditorStyle.h"
#include "Editor/Details/DetailsPanel.h"
#include "Editor/Details/PropertyEditor.h"
#include "Core/Misc/Paths.h"

#include <filesystem>

#include "imgui_internal.h"
#include "Editor/KurisuEd/EditorDragDrop.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SpotLightComponent.h"
#include "Editor/EditorUI/ImGuiRenderer.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/AmbientLightComponent.h"

#include "Components/RotatingMovementComponent.h"
#include "Components/ScriptComponent.h"
#include <commdlg.h>
#include "Components/ProjectileMovementComponent.h"
#include "Components/ParticleSystemComponent.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemFile.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceFile.h"
#include "Animation/AnimBlueprintFile.h"
#include <algorithm>
#include <vector>

#include "Asset/AssetManager.h"
#include "Asset/AssetUtils.h"
#include "Render/Material.h"
#include "Render/Texture2D.h"
#include "Render/SkeletalMesh.h"
#include "Text/Font.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Actor.h"
#include "Core/EngineLog.h"

#include "Editor/KurisuEd/EditorFileUtils.h"
#include "Editor/KurisuEd/EditorUndo.h"
#include "Script/LuaScriptManager.h"


namespace
{
	float GDetailsNameColumnRatio = 0.42f;
	int32 GPropertyGridDepth = 0;
	int32 GStructIndentLevel = 0;

	struct FPropertyGridTableState
	{
		ImGuiTable* Table = nullptr;
		ImGuiID DividerId = 0;
		bool bOpen = false;
	};
	TArray<FPropertyGridTableState> GPropertyGridTableStack;

	void DrawPropertyGridLabel(const char* LabelText)
	{
		if (GStructIndentLevel > 0)
		{
			ImGui::SetCursorPosX(ImGui::GetCursorPosX() + static_cast<float>(GStructIndentLevel) * EditorStyle::Scale(12.0f));
		}
		const float AvailWidth = ImGui::GetContentRegionAvail().x;
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(LabelText);
		if (ImGui::IsItemHovered() && ImGui::CalcTextSize(LabelText).x > AvailWidth)
		{
			ImGui::SetTooltip("%s", LabelText);
		}
	}

	bool BeginPropertyGridTable(const char* TableId)
	{
		const float PaddingX = EditorStyle::Scale(6.0f);
		const float PaddingY = EditorStyle::Scale(4.0f);
		const float AvailWidth = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
		const float MinLabelInner = EditorStyle::Scale(60.0f);
		const float MaxLabelInner = (std::max)(MinLabelInner, AvailWidth - EditorStyle::Scale(90.0f) - PaddingX * 2.0f);
		const float TargetLabelInner = std::clamp(AvailWidth * GDetailsNameColumnRatio - PaddingX * 2.0f, MinLabelInner, MaxLabelInner);

		ImGui::PushStyleColor(ImGuiCol_TableBorderLight, EditorColor::ToVec4(EditorColor::Border));
		ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, EditorColor::ToVec4(EditorColor::BorderStrong));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(PaddingX, PaddingY));

		// ImGuiTableFlags_BordersInner로 행 사이 가로선과 중앙 세로 구분선만 그리고 바깥쪽 테두리는 없앤다.
		// 열 폭 조절은 EndPropertyGridTable의 가로 전용 스플리터로 모든 카테고리에서 동기화한다.
		const ImGuiTableFlags Flags =
			ImGuiTableFlags_BordersInner |
			ImGuiTableFlags_NoSavedSettings |
			ImGuiTableFlags_SizingFixedFit;

		if (!ImGui::BeginTable(TableId, 2, Flags))
		{
			ImGui::PopStyleVar();
			ImGui::PopStyleColor(2);
			return false;
		}

		ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, TargetLabelInner);
		ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize);

		FPropertyGridTableState State;
		State.Table = ImGui::GetCurrentTable();
		State.DividerId = ImGui::GetID("##GridColSplitter");
		State.bOpen = true;
		GPropertyGridTableStack.Add(State);
		++GPropertyGridDepth;
		return true;
	}

	void EndPropertyGridTable()
	{
		if (GPropertyGridTableStack.IsEmpty())
		{
			return;
		}

		const FPropertyGridTableState State = GPropertyGridTableStack.Last();
		GPropertyGridTableStack.Pop();
		if (GPropertyGridDepth > 0)
		{
			--GPropertyGridDepth;
		}

		ImGui::EndTable();

		if (State.bOpen && State.Table && State.Table->Columns.size() >= 2)
		{
			const ImRect TableRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
			const float DividerX = State.Table->Columns[0].MaxX;
			const float HalfHit = EditorStyle::Scale(4.0f);
			const ImRect HitRect(DividerX - HalfHit, TableRect.Min.y, DividerX + HalfHit, TableRect.Max.y);

			ImGui::ItemAdd(HitRect, State.DividerId, nullptr, ImGuiItemFlags_NoNav);
			bool bHovered = false;
			bool bHeld = false;
			ImGui::ButtonBehavior(HitRect, State.DividerId, &bHovered, &bHeld, ImGuiButtonFlags_FlattenChildren | ImGuiButtonFlags_NoNavFocus);

			if (bHovered || bHeld)
			{
				// 옆(가로) 조절 커서만 허용한다.
				ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
				const ImU32 HighlightCol = bHeld ? EditorColor::AccentBright : EditorColor::BorderStrong;
				ImGui::GetWindowDrawList()->AddLine(
					ImVec2(DividerX, TableRect.Min.y),
					ImVec2(DividerX, TableRect.Max.y),
					HighlightCol,
					EditorStyle::Scale(2.0f));
			}

			if (bHeld)
			{
				const float TotalW = (std::max)(1.0f, TableRect.GetWidth());
				const float MinColW = EditorStyle::Scale(60.0f);
				const float MaxColW = (std::max)(MinColW, TotalW - EditorStyle::Scale(90.0f));
				const float NewColW = std::clamp(ImGui::GetIO().MousePos.x - TableRect.Min.x, MinColW, MaxColW);
				GDetailsNameColumnRatio = std::clamp(NewColW / TotalW, 0.15f, 0.80f);
			}
		}

		ImGui::PopStyleVar();
		ImGui::PopStyleColor(2);
	}

	struct FDragSettings
	{
		float Speed;
		float Min;
		float Max;
	};

	FDragSettings GetDragSettings(const FPropertyMeta& Meta, float DefaultSpeed)
	{
		const float Min = (std::max)(Meta.UIMin, Meta.ClampMin);
		const float Max = (std::min)(Meta.UIMax, Meta.ClampMax);
		const bool bHasRange = Min > -FLT_MAX || Max < FLT_MAX;
		return { Meta.Delta > 0.0f ? Meta.Delta : DefaultSpeed, bHasRange ? Min : 0.0f, bHasRange ? Max : 0.0f };
	}

	FString TruncateTextToWidth(const FString& Text, float MaxWidth)
	{
		if (MaxWidth <= 0.0f || Text.empty())
		{
			return {};
		}
		if (ImGui::CalcTextSize(Text.c_str()).x <= MaxWidth)
		{
			return Text;
		}

		const char* Ellipsis = "...";
		if (ImGui::CalcTextSize(Ellipsis).x > MaxWidth)
		{
			return {};
		}

		// UTF-8 문자 경계를 따라 줄여 멀티바이트 이름이 깨지지 않게 한다.
		size_t BestLen = 0;
		for (size_t i = 1; i <= Text.size(); ++i)
		{
			if (i < Text.size() && (static_cast<unsigned char>(Text[i]) & 0xC0) == 0x80)
			{
				continue;
			}
			const FString Candidate = Text.substr(0, i) + Ellipsis;
			if (ImGui::CalcTextSize(Candidate.c_str()).x <= MaxWidth)
			{
				BestLen = i;
			}
			else
			{
				break;
			}
		}
		return BestLen > 0 ? Text.substr(0, BestLen) + Ellipsis : FString(Ellipsis);
	}

	void DrawComponentClassSuffix(
		const FString& DisplayedLabel,
		const FString& FullLabel,
		const char* ClassRawName,
		const ImVec2& RowStart,
		float RowWidth,
		bool bRowHovered)
	{
		if (!ClassRawName || !ClassRawName[0])
		{
			return;
		}

		const FString FullClass = std::format("({})", ClassRawName);
		const float PadRight = ImGui::GetStyle().FramePadding.x;
		const float MinGap = EditorStyle::Scale(8.0f);
		const float RightEdge = RowStart.x + RowWidth - PadRight;
		const float LabelEndX = RowStart.x + ImGui::GetTreeNodeToLabelSpacing()
			+ ImGui::CalcTextSize(DisplayedLabel.c_str()).x;
		const float FullClassWidth = ImGui::CalcTextSize(FullClass.c_str()).x;

		// 기본은 우측 정렬이되 왼쪽 컴포넌트 이름과 겹치지 않게 시작 X를 민다.
		const float TextX = (std::max)(RightEdge - FullClassWidth, LabelEndX + MinGap);
		const float AvailableClassWidth = RightEdge - TextX;
		FString DrawnClass = TruncateTextToWidth(FullClass, AvailableClassWidth);
		if (DrawnClass == "...")
		{
			DrawnClass.clear();
		}
		if (!DrawnClass.empty())
		{
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(TextX, RowStart.y), EditorColor::TextSecondary, DrawnClass.c_str());
		}

		if (bRowHovered && !ImGui::GetDragDropPayload() && (DisplayedLabel != FullLabel || DrawnClass != FullClass))
		{
			ImGui::SetTooltip("%s %s", FullLabel.c_str(), FullClass.c_str());
		}
	}

	int32 ToIntBound(float Value)
	{
		if (Value <= static_cast<float>(INT_MIN)) return INT_MIN;
		if (Value >= static_cast<float>(INT_MAX)) return INT_MAX;
		return static_cast<int32>(Value);
	}

	// 드래그 범위와 별개로, Ctrl+클릭으로 직접 입력한 값도 Clamp 범위로 자른다.
	void ClampToMeta(float* Values, int32 Count, const FPropertyMeta& Meta)
	{
		if (!Meta.HasClamp())
		{
			return;
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Values[Index] = std::clamp(Values[Index], Meta.ClampMin, Meta.ClampMax);
		}
	}

	// Lua 파일을 고르는 대화상자. bCreate면 새 파일 이름을 받는다. 프로젝트 Scripts 폴더에서 시작한다.
	bool PickScriptFile(bool bCreate, const FString& DefaultName, fs::path& OutFile)
	{
		wchar_t Buffer[MAX_PATH] = {};
		wcsncpy_s(Buffer, FPaths::FromUtf8(DefaultName).c_str(), _TRUNCATE);

		std::error_code Ec;
		const fs::path ScriptsDir = fs::absolute(FPaths::ProjectScriptsDir(), Ec);
		fs::create_directories(ScriptsDir, Ec);
		const std::wstring InitialDir = ScriptsDir.wstring();

		OPENFILENAMEW Dialog = {};
		Dialog.lStructSize = sizeof(Dialog);
		Dialog.hwndOwner = GetActiveWindow();
		Dialog.lpstrFile = Buffer;
		Dialog.nMaxFile = static_cast<DWORD>(std::size(Buffer));
		Dialog.lpstrFilter = L"Lua Script (*.lua)\0*.lua\0";
		Dialog.lpstrDefExt = L"lua";
		Dialog.lpstrInitialDir = InitialDir.c_str();
		Dialog.lpstrTitle = bCreate ? L"New Lua Script" : L"Select Lua Script";
		Dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (bCreate ? 0 : OFN_FILEMUSTEXIST);

		if (!(bCreate ? GetSaveFileNameW(&Dialog) : GetOpenFileNameW(&Dialog)))
			return false;

		OutFile = Buffer;
		return true;
	}

	// 대화상자로 스크립트를 고르고 씬에 저장할 프로젝트 상대 경로를 돌려준다. 취소하거나 실패하면 빈 문자열.
	// bCreate에서 이미 있는 파일을 고르면 덮어쓰지 않고 그 파일을 연결한다.
	FString PickScriptPath(bool bCreate, AActor* Actor)
	{
		FString DefaultName;
		if (bCreate)
		{
			const FString& ScenePath = FEditorFileUtils::CurrentScenePath;
			if (!ScenePath.empty())
				DefaultName = FLuaScriptManager::FromPath(FLuaScriptManager::ToPath(ScenePath).stem()) + "_";
			DefaultName += Actor->GetName() + ".lua";
		}

		fs::path File;
		if (!PickScriptFile(bCreate, DefaultName, File))
			return {};

		const FString Key = FLuaScriptManager::MakeScriptKey(File);
		if (bCreate && !FLuaScriptManager::Get().CreateScriptFromTemplate(Key))
			return {};

		if (FPaths::FromUtf8(Key).is_absolute())
			KRS_LOG(Warning, "[Lua] {} is outside the project. It is saved as an absolute path and will not be packaged.", Key);
		return Key;
	}

	// 선택한 ScriptComponent의 스크립트 경로 입력과 파일 연결 버튼.
	void DrawScriptSlot(UScriptComponent* Script, ImFont* CustomFont)
	{
		const FString Path = Script->GetScriptPath();
		const bool bHasPath = !Path.empty();

		// 입력 중에는 버퍼를 유지하고, Enter나 포커스 이동으로 확정할 때만 경로를 바꾼다.
		// 글자마다 반영하면 치는 도중의 경로가 정규화되고 플레이 중에는 스크립트가 매번 다시 시작된다.
		static char Buffer[512] = {};
		static const UScriptComponent* BufferOwner = nullptr;
		static FString BufferSource;
		if (BufferOwner != Script || BufferSource != Path)
		{
			strncpy_s(Buffer, Path.c_str(), _TRUNCATE);
			BufferOwner = Script;
			BufferSource = Path;
		}

		if (BeginPropertyGridTable("ScriptProperties"))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawPropertyGridLabel("Script Path");

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			if (CustomFont) ImGui::PushFont(CustomFont);
			ImGui::InputTextWithHint("##ScriptPath", "Content/Scripts/MyScript.lua", Buffer, sizeof(Buffer));
			const bool bCommitted = ImGui::IsItemDeactivatedAfterEdit();
			if (CustomFont) ImGui::PopFont();
			if (bCommitted)
			{
				Script->SetScriptPath(Buffer);
				BufferOwner = nullptr;   // 정규화된 경로로 다시 채운다
				EndPropertyGridTable();
				return;
			}

			std::error_code Ec;
			if (bHasPath && !fs::exists(FLuaScriptManager::ToPath(Path), Ec))
			{
				ImGui::TextColored(EditorColor::ToVec4(EditorColor::Error), "File not found");
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawPropertyGridLabel("Actions");

			ImGui::TableSetColumnIndex(1);
			// 기존 파일 연결
			if (ImGui::Button("Browse"))
			{
				const FString Picked = PickScriptPath(false, Script->GetOwner());
				if (!Picked.empty())
					Script->SetScriptPath(Picked);
			}
			ImGui::SameLine();
			// 템플릿에서 새 파일을 만들어 연결
			if (ImGui::Button("New"))
			{
				const FString Picked = PickScriptPath(true, Script->GetOwner());
				if (!Picked.empty())
					Script->SetScriptPath(Picked);
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(!bHasPath);
			if (ImGui::Button("Edit"))
				FLuaScriptManager::Get().OpenScriptInEditor(Path);
			ImGui::SameLine();
			// 연결만 끊는다. 파일은 남는다.
			if (ImGui::Button("Clear"))
				Script->SetScriptPath({});
			ImGui::EndDisabled();

			EndPropertyGridTable();
		}
	}

	bool DrawAxisControl(const FString& _label, float& _value, float _speed, float _minValue, float _maxValue, float _resetValue, FVector4 _color)
	{
		bool isValueChanged = false;

		float lineHeight = ImGui::GetFrameHeight();
		ImVec4 color(_color.X, _color.Y, _color.Z, _color.W);
		ImVec2 buttonSize = { lineHeight + EditorStyle::Scale(2.0f) , lineHeight };

		// 각 버튼의 색상 설정
		ImGui::PushStyleColor(ImGuiCol_Button, color);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ color.x - 0.1f, color.y - 0.1f, color.z - 0.1f, 1.0f });

		// 굵은 폰트 적용
		if (ImGui::Button(_label.c_str(), buttonSize))
		{
			_value = _resetValue;
			isValueChanged = true;
		}
		ImGui::PopStyleColor(3);

		// 드래그 슬라이더
		ImGui::SameLine();
		std::string dragID = "##" + _label;
		isValueChanged |= ImGui::DragFloat(dragID.c_str(), &_value, _speed, _minValue, _maxValue, "%.2f");

		return isValueChanged;

	}

	bool DrawVector3Controller(const FString& _label, float* _values, float _resetValue, float _columnWidth)
	{
		(void)_columnWidth;
		ImGuiIO& io = ImGui::GetIO();
		auto boldFont = io.FontDefault ? io.FontDefault : io.Fonts->Fonts[0];

		bool isVectorChanged = false;
		const bool bUseOwnTable = (GPropertyGridDepth == 0);

		ImGui::PushID(_label.c_str());
		if (!bUseOwnTable || BeginPropertyGridTable(_label.c_str()))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawPropertyGridLabel(_label.c_str());

			ImGui::TableSetColumnIndex(1);
			// [컨트롤러 UI 코드] // 오른쪽 열: 컨트롤러
			const float Gap = EditorStyle::Scale(2.0f);
			const float ButtonWidth = ImGui::GetFrameHeight() + EditorStyle::Scale(2.0f);
			const float Available = ImGui::GetContentRegionAvail().x;
			const bool bStackAxes = Available < EditorStyle::Scale(165.0f);
			const float ValueWidth = bStackAxes
				? ImMax(1.0f, Available - ButtonWidth - Gap)
				: ImMax(3.0f, Available - 3.0f * ButtonWidth - 5.0f * Gap);
			ImGui::PushMultiItemsWidths(bStackAxes ? 1 : 3, ValueWidth);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ Gap, ImGui::GetStyle().ItemSpacing.y });
			ImGui::PushFont(boldFont);

			isVectorChanged |= DrawAxisControl("X", _values[0], 0.1f, 0.0f, 0.0f, _resetValue, { 0.8f, 0.1f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			if (bStackAxes) ImGui::PushItemWidth(ValueWidth); else ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Y", _values[1], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.8f, 0.1f, 1.0f });

			ImGui::PopItemWidth();

			if (bStackAxes) ImGui::PushItemWidth(ValueWidth); else ImGui::SameLine();
			isVectorChanged |= DrawAxisControl("Z", _values[2], 0.1f, 0.0f, 0.0f, _resetValue, { 0.1f, 0.1f, 0.8f, 1.0f });

			ImGui::PopItemWidth();

			ImGui::PopFont();
			ImGui::PopStyleVar();
			if (bUseOwnTable)
			{
				EndPropertyGridTable();
			}
		}

		ImGui::PopID();

		return isVectorChanged;
	}

	// FRotator의 V는 (Pitch, Yaw, Roll) 순서지만 엔진은 Roll=X, Pitch=Y, Yaw=Z축이므로
	// X/Y/Z 칸이 실제 회전축과 맞도록 재배열해 편집한다.
	bool DrawRotatorAsXYZ(const FString& _label, FRotator& _rotator)
	{
		float Euler[3] = { _rotator.Roll, _rotator.Pitch, _rotator.Yaw };
		const bool isChanged = DrawVector3Controller(_label, Euler, 0.0f, 55.0f);
		if (isChanged)
		{
			_rotator.Roll = Euler[0];
			_rotator.Pitch = Euler[1];
			_rotator.Yaw = Euler[2];
		}
		return isChanged;
	}

	bool DrawTextureSlot(UMaterial** MaterialPtr, int32 SlotIndex)
	{
		bool bChanged = false;
		const float ThumbnailSize = EditorStyle::Scale(64.0f);

		UMaterial* Material = *MaterialPtr;
		UTexture2D* Texture = nullptr;
		if (Material && SlotIndex < static_cast<int32>(Material->Textures.Num()))
		{
			Texture = Material->Textures[SlotIndex];
		}

		ImGui::PushID(SlotIndex);

		if (Texture && Texture->GetResource())
		{
			ImGui::Image(
				(ImTextureID)Texture->GetResource()->GetSRV(),
				{ ThumbnailSize, ThumbnailSize });
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		// 바로 위 위젯이 드롭을 받는다
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Texture))
			{
				UTexture2D* DroppedTexture = *static_cast<UTexture2D**>(Payload->Data);

				// 머티리얼이 없으면 기본 머티리얼에서 시작한다
				if (!Material)
				{
					Material = UMaterial::CreateInstance(UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial"));
					*MaterialPtr = Material;
				}
				// 공유 에셋이면 지금 복제한다.
				// 이 시점까지 미루면 손대지 않은 오브젝트는 계속 공유 에셋을 쓴다.
				else if (Material->bIsInstance == false)
				{
					Material = UMaterial::CreateInstance(Material);
					*MaterialPtr = Material;
				}

				if (Material)
				{
					// 슬롯이 배열 범위 밖이면 그 자리까지 늘려 준다
					while (Material->Textures.Num() <= SlotIndex)
					{
						Material->Textures.Add(nullptr);
					}

					Material->Textures[SlotIndex] = DroppedTexture;
					Texture = DroppedTexture;
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::SameLine();

		ImGui::BeginGroup();
		ImGui::Text("Slot %d", SlotIndex);

		if (Texture)
		{
			ImGui::TextWrapped("%s", Texture->GetPath().c_str());
		}
		else
		{
			ImGui::TextDisabled("(Empty)");
		}
		ImGui::EndGroup();

		ImGui::PopID();

		return bChanged;
	}

	FString GetAssetDisplayName(const FString& Path)
	{
		fs::path AssetPath(Path);
		if (AssetPath.stem() == "Atlas")
		{
			return AssetPath.parent_path().filename().string();
		}
		return AssetPath.stem().string();
	}

	void DrawFontSlot(UObject* Owner, UFont** FontPtr)
	{
		UFont* Current = *FontPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##Font", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UFont> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
						TextComponent->SetFont(*Itr);
					else
						*FontPtr = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}

			// 아직 로드되지 않은 폰트는 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Font))
			{
				ImGui::PushID(ItemPath.c_str());
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
				{
					if (UFont* Font = UAssetManager::GetAssetByPath<UFont>(ItemPath))
					{
						if (UTextRenderComponent* TextComponent = Cast<UTextRenderComponent>(Owner))
							TextComponent->SetFont(Font);
						else
							*FontPtr = Font;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	void DrawSkeletalMeshSlot(USkinnedMeshComponent* Component)
	{
		USkeletalMesh* Current = Component->GetSkeletalMesh();
		const FString CurrentPath = Current ? Current->GetPath() : "";
		const FString CurrentName = Current ? (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath)) : "None";
		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen = ImGui::BeginCombo("##SkeletalMesh", CurrentName.c_str());
		if (ImGui::IsItemHovered() && Current) ImGui::SetTooltip("%s", CurrentPath.c_str());
		if (!bOpen) return;
		if (ImGui::Selectable("None", Current == nullptr)) Component->SetSkeletalMesh(nullptr);
		for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
		{
			const FString Path = Itr->GetPath();
			if (Path.empty()) continue;
			const bool bSelected = *Itr == Current;
			ImGui::PushID(*Itr);
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected)) Component->SetSkeletalMesh(*Itr);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			if (bSelected) ImGui::SetItemDefaultFocus();
			ImGui::PopID();
		}

		// 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
		for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::SkeletalMesh))
		{
			ImGui::PushID(ItemPath.c_str());
			if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
			{
				if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(ItemPath))
				{
					Component->SetSkeletalMesh(Mesh);
				}
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

    void DrawAnimBlueprintSlot(USkeletalMeshComponent* Component)
    {
        static std::vector<FString> Paths;
        static FString ScanMessage;
        const FString& CurrentPath = Component->GetAnimBlueprintPath();
        const FString Name = CurrentPath.empty() ? "None" : GetAssetDisplayName(CurrentPath);
        ImGui::SetNextItemWidth(-1.0f);

        if (ImGui::BeginCombo("##AnimBlueprintAsset", Name.c_str()))
        {
            if (ImGui::IsWindowAppearing())
            {
                Paths.clear();
                ScanMessage.clear();
                std::error_code Error;
                std::filesystem::recursive_directory_iterator It(FPaths::ProjectContentDir(), Error);
                const std::filesystem::recursive_directory_iterator End;

                while (!Error && It != End)
                {
                    if (It->is_regular_file(Error))
                    {
                        const FString Path = FPaths::ToUtf8(It->path());

                        if (AnimBlueprintFile::IsAnimBlueprintPath(Path))
                        {
                            Paths.push_back(FAssetUtils::GetAssetKey(Path));
                        }
                    }

                    if (!Error)
                    {
                        It.increment(Error);
                    }
                }

                std::sort(Paths.begin(), Paths.end());

                if (Error)
                {
                    ScanMessage = "Cannot read the asset folder: " + Error.message();
                }
            }

            if (ImGui::Selectable("None", CurrentPath.empty()))
            {
                Component->SetAnimBlueprint(FString());
            }

            for (const FString& Path : Paths)
            {
                ImGui::PushID(Path.c_str());

                if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), CurrentPath == Path))
                {
                    Component->SetAnimBlueprint(Path);
                }

                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", Path.c_str());
                }

                ImGui::PopID();
            }

            if (!ScanMessage.empty())
            {
                ImGui::TextWrapped("%s", ScanMessage.c_str());
            }
            else if (Paths.empty())
            {
                ImGui::TextUnformatted("No .animblueprint files in Assets.");
            }

            ImGui::EndCombo();
        }

        if (!Component->GetAnimBlueprintPath().empty() && ImGui::SmallButton("Reload Animation Blueprint"))
        {
            Component->ReloadAnimBlueprint();
        }

        const FString& Error = Component->GetAnimBlueprintError();

        if (!Error.empty())
        {
            ImGui::TextWrapped("%s", Error.c_str());
        }
    }

	void DrawAnimationSlot(USkeletalMeshComponent* Component)
	{
		auto* Instance = Component->GetSingleNodeInstance();
		UAnimationAsset* Current = Instance ? Instance->GetCurrentAsset() : nullptr;

		const FString Name = Current ? GetAssetDisplayName(Current->GetAssetPath()) : "None";

		ImGui::SetNextItemWidth(-1.0f);
		if (!ImGui::BeginCombo("##SingleAnimAsset", Name.c_str()))
		{
			return;
		}

		if (ImGui::Selectable("None", Current == nullptr))
		{
			Component->SetAnimation(nullptr);
			Component->ResetPose();
			Component->RefreshBoneTransformsNow();
		}

		USkeletalMesh* Mesh = Component->GetSkeletalMesh();
		if (Mesh)
		{
			FString FilePath = FAssetUtils::ResolveAssetFilePath(Mesh->GetPath());

			if (FilePath.empty())
			{
				FilePath = Mesh->GetPath();
			}

			const std::filesystem::path Directory = std::filesystem::path(FilePath).parent_path();
			if (!Directory.empty() && std::filesystem::is_directory(Directory))
			{
				for (const auto& Entry : std::filesystem::directory_iterator(Directory))
				{
					if (!Entry.is_regular_file())
					{
						continue;
					}

					const FString Path = Entry.path().generic_string();
					if (!AnimSequenceFile::IsAnimSequencePath(Path))
					{
						continue;
					}

					ImGui::PushID(Path.c_str());

					if (ImGui::Selectable(GetAssetDisplayName(Path).c_str()))
					{
						if (UAnimSequence* Sequence = UAssetManager::LoadAnimSequence(Path))
						{
							Component->PlayAnimation(Sequence, true);
						}
					}
					ImGui::PopID();
				}
			}
		}
		ImGui::EndCombo();
	}

	void DrawMeshSlot(UObject* Owner, UStaticMesh** StaticMeshPtr)
	{
		UStaticMesh* Current = *StaticMeshPtr;

		FString CurrentPath = Current ? Current->GetPath() : "";
		FString CurrentFileName = Current ? GetAssetDisplayName(CurrentPath) : "None";

		ImGui::SetNextItemWidth(-1.0f);

		bool bOpen = ImGui::BeginCombo("##StaticMesh", CurrentFileName.c_str());
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			for (TObjectIterator<UStaticMesh> Itr; Itr; ++Itr)
			{
				bool bSelected = (*Itr == Current);

				FString ItemPath = Itr->GetPath();
				FString ItemFileName = GetAssetDisplayName(ItemPath);

				ImGui::PushID(*Itr);

				if (ImGui::Selectable(ItemFileName.c_str(), bSelected))
				{
					if (ImGui::IsItemHovered())
					{
						ImGui::SetTooltip("%s", ItemPath.c_str());
					}

					// 메모리를 직접 바꾸지 않고 SetFont를 거친다
					if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
						MeshComponent->SetStaticMesh(*Itr);
					else
						*StaticMeshPtr = *Itr;
				}
				ImGui::PopID();
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}

			// 아직 로드되지 않은 메시는 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::StaticMesh))
			{
				ImGui::PushID(ItemPath.c_str());
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
				{
					if (UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>(ItemPath))
					{
						if (UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(Owner))
							MeshComponent->SetStaticMesh(Mesh);
						else
							*StaticMeshPtr = Mesh;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
	}

	// ParticleSystem Template
	bool DrawParticleSystemPicker(UParticleSystem* Current, bool bMixed, const char* Id, const std::function<void(UParticleSystem*)>& OnPick)
	{
		static std::vector<FString> Paths;
		static FString ScanMessage;

		if (bMixed)
		{
			Current = nullptr;
		}
		const FString CurrentPath = Current ? Current->GetAssetPath() : "";
		const FString CurrentName = bMixed ? "Multiple Values"
			: (Current ? (CurrentPath.empty() ? Current->GetName() : GetAssetDisplayName(CurrentPath)) : "None");

		bool bChanged = false;
		auto Apply = [&](UParticleSystem* NewTemplate)
		{
			if (!bMixed && NewTemplate == Current)
			{
				return;
			}
			OnPick(NewTemplate);
			bChanged = true;
		};

		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen = ImGui::BeginCombo(Id, CurrentName.c_str());
		if (ImGui::IsItemHovered() && !CurrentPath.empty() && !ImGui::GetDragDropPayload())
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::ParticleSystem))
			{
				const FString DroppedPath(static_cast<const char*>(Payload->Data));
				if (UParticleSystem* Dropped = UAssetManager::LoadParticleSystem(DroppedPath))
				{
					Apply(Dropped);
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (!bOpen)
		{
			return bChanged;
		}

		if (ImGui::IsWindowAppearing())
		{
			Paths.clear();
			ScanMessage.clear();
			std::error_code Error;
			std::filesystem::recursive_directory_iterator It(FPaths::ProjectContentDir(), Error);
			const std::filesystem::recursive_directory_iterator End;

			while (!Error && It != End)
			{
				if (It->is_regular_file(Error))
				{
					const FString Path = FPaths::ToUtf8(It->path());
					if (ParticleSystemFile::IsParticleSystemPath(Path))
					{
						Paths.push_back(FAssetUtils::GetAssetKey(Path));
					}
				}
				if (!Error)
				{
					It.increment(Error);
				}
			}

			std::sort(Paths.begin(), Paths.end());

			if (Error)
			{
				ScanMessage = "Cannot read the asset folder: " + Error.message();
			}
		}

		if (ImGui::Selectable("None", !bMixed && Current == nullptr))
		{
			Apply(nullptr);
		}

		const FString CurrentKey = CurrentPath.empty() ? FString() : FAssetUtils::GetAssetKey(CurrentPath);
		for (const FString& Path : Paths)
		{
			ImGui::PushID(Path.c_str());
			const bool bSelected = !CurrentKey.empty() && CurrentKey == Path;
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected))
			{
				if (UParticleSystem* Picked = UAssetManager::LoadParticleSystem(Path))
				{
					Apply(Picked);
				}
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Path.c_str());
			}
			if (bSelected)
			{
				ImGui::SetItemDefaultFocus();
			}
			ImGui::PopID();
		}

		if (!ScanMessage.empty())
		{
			ImGui::TextWrapped("%s", ScanMessage.c_str());
		}
		else if (Paths.empty())
		{
			ImGui::TextUnformatted("No particle system files in Assets.");
		}

		ImGui::EndCombo();
		return bChanged;
	}

	void ApplyParticleTemplate(UObject* Owner, UParticleSystem** TemplatePtr, UParticleSystem* NewTemplate)
	{
		if (UParticleSystemComponent* Component = Cast<UParticleSystemComponent>(Owner))
			Component->SetTemplate(NewTemplate);
		else
			*TemplatePtr = NewTemplate;
	}

	bool DrawParticleSystemSlot(UObject* Owner, UParticleSystem** TemplatePtr)
	{
		return DrawParticleSystemPicker(*TemplatePtr, false, "##ParticleSystem",
			[Owner, TemplatePtr](UParticleSystem* Picked) { ApplyParticleTemplate(Owner, TemplatePtr, Picked); });
	}

	UMaterial* EnsureMaterialOverride(UMeshComponent* MeshComponent, int32 Slot, UMaterial* Effective, UMaterial* Override)
	{
		// 이 컴포넌트 전용 인스턴스가 아니면 지금 적용 중인 머티리얼을 복제해서 덮어쓰기로 만든다.
		// 메시의 머티리얼은 같은 메시를 쓰는 모든 액터가 공유하므로 직접 고치면 안 된다.
		if (Override && Override->bIsInstance)	return Override;

		UMaterial* Source = Effective ? Effective : UAssetManager::GetAssetByPath<UMaterial>("DefaultMaterial");

		Override = UMaterial::CreateInstance(Source);
		MeshComponent->SetMaterial(Slot, Override);

		return Override;
	}

	bool SetMaterialTexture(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, UTexture2D* Texture, UMaterial*& Effective, UMaterial*& Override) {
		UTexture2D* Current{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };
		if (Current == Texture) {
			return false;
		}

		Override = EnsureMaterialOverride(MeshComponent, MaterialSlot, Effective, Override);
		if (!Override) {
			return false;
		}

		while (!Override->Textures.IsValidIndex(TextureSlot)) {
			Override->Textures.Add(nullptr);
		}
		Override->Textures[TextureSlot] = Texture;
		Effective = Override;
		return true;
	}

	bool DrawTexturePicker(UTexture2D* Current, const char* Label,
		const std::function<void(UTexture2D*)>& OnPick)
	{
		const float ThumbnailSize{ EditorStyle::Scale(64.0f) };
		const float ItemThumbnailSize{ EditorStyle::Scale(24.0f) };

		bool bChanged{ false };

		if (Current && Current->GetResource())
		{
			ImGui::Image((ImTextureID)Current->GetResource()->GetSRV(), { ThumbnailSize, ThumbnailSize });
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", Current->GetPath().c_str());
			}
		}
		else
		{
			ImGui::Button("Empty", { ThumbnailSize, ThumbnailSize });
		}

		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload * Payload{ ImGui::AcceptDragDropPayload(EditorDragDrop::Texture) })
			{
				UTexture2D* DroppedTexture{ *static_cast<UTexture2D**>(Payload->Data) };
				if (DroppedTexture != Current)
				{
					OnPick(DroppedTexture);
					bChanged = true;
				}
			}
			ImGui::EndDragDropTarget();
		}

		const FString CurrentPath{ Current ? Current->GetPath() : "" };
		const FString CurrentFileName{ Current ? GetAssetDisplayName(CurrentPath) : "None" };

		ImGui::SetNextItemWidth(-1.0f);
		const bool bOpen{ ImGui::BeginCombo(Label, CurrentFileName.c_str()) };
		if (ImGui::IsItemHovered() && Current)
		{
			ImGui::SetTooltip("%s", CurrentPath.c_str());
		}

		if (bOpen)
		{
			if (ImGui::Selectable("None", Current == nullptr) && Current != nullptr)
			{
				OnPick(nullptr);
				bChanged = true;
			}

			// 경로 없는 런타임 텍스처(렌더타겟 등)는 목록에서 뺀다.
			for (TObjectIterator<UTexture2D> Iterator{}; Iterator; ++Iterator)
			{
				UTexture2D* ItemTexture{ *Iterator };
				if (!ItemTexture->GetResource() || ItemTexture->GetPath().empty())
				{
					continue;
				}

				const bool bSelected{ ItemTexture == Current };
				const FString ItemPath{ ItemTexture->GetPath() };
				const FString ItemFileName{ GetAssetDisplayName(ItemPath) };

				ImGui::PushID(ItemTexture);
				ImGui::Image((ImTextureID)ItemTexture->GetResource()->GetSRV(), { ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(ItemFileName.c_str(), bSelected, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize })
					&& !bSelected)
				{
					OnPick(ItemTexture);
					bChanged = true;
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
				ImGui::PopID();
			}

			// 아직 로드되지 않은 텍스처는 썸네일 없이 경로만 보여 주고 고를 때 로드한다
			for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Texture))
			{
				ImGui::PushID(ItemPath.c_str());
				ImGui::Dummy({ ItemThumbnailSize, ItemThumbnailSize });
				ImGui::SameLine();
				if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false, ImGuiSelectableFlags_None, { 0.0f, ItemThumbnailSize }))
				{
					if (UTexture2D* ItemTexture{ UAssetManager::GetAssetByPath<UTexture2D>(ItemPath) })
					{
						OnPick(ItemTexture);
						bChanged = true;
					}
				}
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", ItemPath.c_str());
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}

		return bChanged;
	}

	void DrawMaterialTextureSlot(UMeshComponent* MeshComponent, int32 MaterialSlot, int32 TextureSlot, const char* Label, UMaterial*& Effective, UMaterial*& Override) {
		const float ThumbnailSize{ EditorStyle::Scale(64.0f) };
		const float ItemThumbnailSize{ EditorStyle::Scale(24.0f) };
		UTexture2D* Texture{ Effective && Effective->Textures.IsValidIndex(TextureSlot) ? Effective->Textures[TextureSlot] : nullptr };

		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		DrawPropertyGridLabel(Label);
		ImGui::TableSetColumnIndex(1);
		ImGui::PushID(TextureSlot);

		DrawTexturePicker(Texture, "##Texture",
			[&](UTexture2D* Picked)
			{
				SetMaterialTexture(MeshComponent, MaterialSlot, TextureSlot, Picked, Effective, Override);
			});

		ImGui::PopID();
	}

	// 메시 컴포넌트의 머티리얼 슬롯을 한 줄씩 그린다.
	// 텍스처를 드롭하면 그 슬롯의 덮어쓰기에 반영하고, 메시 에셋의 머티리얼은 건드리지 않는다.
	void DrawMaterialSlots(UMeshComponent* MeshComponent)
	{
		const int32 NumSlots = MeshComponent->GetNumMaterials();
		if (NumSlots <= 0)
		{
			return;
		}

		if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
		{
			return;
		}

		// 메쉬 내의 매터리얼 별 Slot 순회
		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);

			FString SlotLabel = std::format("[{}] {}", Slot, MeshComponent->GetMaterialSlotName(Slot));
			bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			UMaterial* Override = MeshComponent->GetOverrideMaterial(Slot);
			UMaterial* Effective = MeshComponent->GetMaterial(Slot);

			ImGui::SameLine();

			if (Override)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();

				if (ImGui::SmallButton("Reset"))
				{
					MeshComponent->SetMaterial(Slot, nullptr);
					Override = nullptr;
					Effective = MeshComponent->GetMaterial(Slot);
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (BeginPropertyGridTable("MaterialProperties"))
				{
					DrawMaterialTextureSlot(MeshComponent, Slot, 0, "Texture", Effective, Override);
					DrawMaterialTextureSlot(MeshComponent, Slot, 1, "Normal Map", Effective, Override);

					ImGui::TableNextRow();

					// 3: Base Color
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Base Color");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector4 TempColor = Effective ? Effective->BaseColor : FVector4(1.0f, 1.0f, 1.0f, 1.0f);
					if (ImGui::ColorEdit4("##BaseColor", &TempColor.X))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->BaseColor = TempColor;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Shininess");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					float TempShininess = Effective ? Effective->Shininess : 32.0f;
					if (ImGui::DragFloat("##Shininess", &TempShininess, 0.5f, 1.0f, 256.0f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->Shininess = TempShininess;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 4: UV Scroll Speed
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("UV Scroll");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					FVector2 TempUVScrollSpeed = Effective ? Effective->UVScrollSpeed : FVector2();
					if (ImGui::DragFloat2("##UVScrollSpeed", &TempUVScrollSpeed.X, 0.01f))
					{
						Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
						if (Override)
						{
							Override->UVScrollSpeed = TempUVScrollSpeed;
							Effective = Override;
						}
					}

					ImGui::TableNextRow();

					// 5: Sampler State
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Sampler");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					int SamplerIndex = Effective ? static_cast<int>(Effective->SamplerState) : static_cast<int>(ESamplerState::LinearClamp);
					if (ImGui::BeginCombo("##SamplerState", SamplerItems[SamplerIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
						{
							if (ImGui::Selectable(SamplerItems[i], SamplerIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->SamplerState = static_cast<ESamplerState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					ImGui::TableNextRow();

					// 6: Blend State
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Blend Mode");

					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					int BlendIndex = Effective ? static_cast<int>(Effective->BlendState) : static_cast<int>(EBlendState::Opaque);
					if (ImGui::BeginCombo("##BlendState", BlendItems[BlendIndex]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							if (ImGui::Selectable(BlendItems[i], BlendIndex == i))
							{
								Override = EnsureMaterialOverride(MeshComponent, Slot, Effective, Override);
								if (Override)
								{
									Override->BlendState = static_cast<EBlendState>(i);
									Effective = Override;
								}
							}
						}
						ImGui::EndCombo();
					}

					EndPropertyGridTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();

			ImGui::Separator();
		}
	}

	uint32 ToUIntBound(float Value)
	{
		if (Value <= 0.0f) return 0;
		if (Value >= static_cast<float>(UINT_MAX)) return UINT_MAX;
		return static_cast<uint32>(Value);
	}

	bool DrawPropertyRow(UObject* Object, void* Container, const FProperty& Property, ImFont* CustomFont, const FPropertyMeta* MetaOverride = nullptr);

	TMap<const UScriptStruct*, FPropertyEditor::FStructDrawer>& GetStructDrawers()
	{
		static TMap<const UScriptStruct*, FPropertyEditor::FStructDrawer> Drawers;
		return Drawers;
	}

	// 프로퍼티 타입에 맞는 위젯 하나를 그린다. 값이 바뀌면 true
	// UE도 FProperty는 위젯을 모르고, 에디터(PropertyEditor 모듈)가 CastField로 타입을 보고 위젯을 고른다.
	bool DrawPropertyValue(UObject* Object, const FProperty& Property, void* ValuePtr, const char* Label, ImFont* CustomFont, const FPropertyMeta* MetaOverride = nullptr)
	{
		auto MetaOf = [MetaOverride](const FNumericProperty& Numeric) -> const FPropertyMeta& { return MetaOverride ? *MetaOverride : Numeric.Meta; };

		if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(&Property))
		{
			float* Value = &FloatProperty->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(MetaOf(*FloatProperty), 0.1f);
			const bool bChanged = ImGui::DragFloat(Label, Value, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(Value, 1, MetaOf(*FloatProperty));
			return bChanged;
		}
		if (const FIntProperty* IntProperty = CastField<FIntProperty>(&Property))
		{
			int32* Value = &IntProperty->GetPropertyValue(ValuePtr);
			const FPropertyMeta& Meta = MetaOf(*IntProperty);
			const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
			const bool bChanged = ImGui::DragInt(Label, Value, Drag.Speed, ToIntBound(Drag.Min), ToIntBound(Drag.Max));
			if (bChanged && Meta.HasClamp())
			{
				*Value = ToIntBound(std::clamp(static_cast<float>(*Value), Meta.ClampMin, Meta.ClampMax));
			}
			return bChanged;
		}
		if (const FUInt32Property* UIntProperty = CastField<FUInt32Property>(&Property))
		{
			uint32* Value = &UIntProperty->GetPropertyValue(ValuePtr);
			const FPropertyMeta& Meta = MetaOf(*UIntProperty);
			const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
			const uint32 Min = ToUIntBound(Drag.Min);
			const uint32 Max = Drag.Min < Drag.Max ? ToUIntBound(Drag.Max) : UINT_MAX;
			const bool bChanged = ImGui::DragScalar(Label, ImGuiDataType_U32, Value, Drag.Speed, &Min, &Max);
			if (bChanged && Meta.HasClamp())
			{
				*Value = ToUIntBound(std::clamp(static_cast<float>(*Value), Meta.ClampMin, Meta.ClampMax));
			}
			return bChanged;
		}
		if (const FVectorProperty* VectorProperty = CastField<FVectorProperty>(&Property))
		{
			FVector& Value = VectorProperty->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(MetaOf(*VectorProperty), 0.1f);
			const bool bChanged = ImGui::DragFloat3(Label, Value.V, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(Value.V, 3, MetaOf(*VectorProperty));
			return bChanged;
		}
		if (const FRotatorProperty* RotatorProperty = CastField<FRotatorProperty>(&Property))
		{
			FRotator& Value = RotatorProperty->GetPropertyValue(ValuePtr);
			// Transform의 Rotation과 같은 X/Y/Z 축 순서로 보여준다.
			float Euler[3] = { Value.Roll, Value.Pitch, Value.Yaw };
			const FDragSettings Drag = GetDragSettings(MetaOf(*RotatorProperty), 0.1f);
			if (ImGui::DragFloat3(Label, Euler, Drag.Speed, Drag.Min, Drag.Max))
			{
				ClampToMeta(Euler, 3, MetaOf(*RotatorProperty));
				Value.Roll = Euler[0];
				Value.Pitch = Euler[1];
				Value.Yaw = Euler[2];
				return true;
			}
			return false;
		}
		if (const FVector4Property* Vector4Property = CastField<FVector4Property>(&Property))
		{
			FVector4& Value = Vector4Property->GetPropertyValue(ValuePtr);
			const FDragSettings Drag = GetDragSettings(MetaOf(*Vector4Property), 0.1f);
			const bool bChanged = ImGui::DragFloat4(Label, &Value.X, Drag.Speed, Drag.Min, Drag.Max);
			if (bChanged) ClampToMeta(&Value.X, 4, MetaOf(*Vector4Property));
			return bChanged;
		}
		if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(&Property))
		{
			return ImGui::Checkbox(Label, &BoolProperty->GetPropertyValue(ValuePtr));
		}
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(&Property))
		{
			// UENUM: 이름 목록으로 콤보박스를 그린다. UMETA(Hidden) 항목은 목록에서 뺀다.
			const FEnumInfo* EnumInfo = EnumProperty->Enum;
			if (!EnumInfo)
			{
				ImGui::TextDisabled("(No enum info)");
				return false;
			}
			bool bChanged = false;
			const int64 Current = EnumProperty->GetValue(ValuePtr);
			const FString Preview = EnumInfo->ToName(Current);
			if (ImGui::BeginCombo(Label, Preview.c_str()))
			{
				for (const FEnumEntry& Entry : EnumInfo->Entries)
				{
					if (Entry.bHidden)
					{
						continue;
					}
					const bool bSelected = Entry.Value == Current;
					if (ImGui::Selectable(Entry.Name.c_str(), bSelected) && !bSelected)
					{
						EnumProperty->SetValue(ValuePtr, Entry.Value);
						bChanged = true;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return bChanged;
		}
		if (const FColorProperty* ColorProperty = CastField<FColorProperty>(&Property))
		{
			FColor& C = ColorProperty->GetPropertyValue(ValuePtr);
			float V[4] = { C.R / 255.f, C.G / 255.f, C.B / 255.f, C.A / 255.f };
			if (ImGui::ColorEdit4(Label, V))
			{
				C = FColor(uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
				return true;
			}
			return false;
		}
		if (const FLinearColorProperty* LinearColorProperty = CastField<FLinearColorProperty>(&Property))
		{
			return ImGui::ColorEdit4(Label, LinearColorProperty->GetPropertyValue(ValuePtr).RGBA);
		}
		if (const FStrProperty* StrProperty = CastField<FStrProperty>(&Property))
		{
			FString& Value = StrProperty->GetPropertyValue(ValuePtr);

			char Buffer[256] = {};
			strncpy_s(Buffer, Value.c_str(), sizeof(Buffer) - 1);
			if (CustomFont) ImGui::PushFont(CustomFont);
			const bool bChanged = ImGui::InputText(Label, Buffer, sizeof(Buffer));
			if (bChanged) Value = Buffer;
			if (CustomFont) ImGui::PopFont();
			return bChanged;
		}
		if (const FTransformProperty* TransformProperty = CastField<FTransformProperty>(&Property))
		{
			FTransform& Value = TransformProperty->GetPropertyValue(ValuePtr);
			ImGui::PushID(ValuePtr);


			bool bChanged = false;
			bChanged |= DrawVector3Controller("Location", Value.Location.V, 0.0f, 55.0f);
			// 드래그 중 값이 튀지 않도록 프로퍼티마다 마지막 오일러 값을 기억한다
			static TMap<const void*, FRotationConversionCache> RotationCaches;
			FRotationConversionCache& RotationCache = RotationCaches[ValuePtr];
			FRotator Euler = RotationCache.QuatToRotator(Value.Rotation);
			if (DrawRotatorAsXYZ("Rotation", Euler))
			{
				Value.Rotation = RotationCache.RotatorToQuat(Euler);
				bChanged = true;
			}
			bChanged |= DrawVector3Controller("Scale", Value.Scale.V, 1.0f, 55.0f);
			ImGui::PopID();
			return bChanged;
		}
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(&Property))
		{
			UObject** ObjPtr = &ObjectProperty->GetPropertyValue(ValuePtr);
			const UClass* PropertyClass = ObjectProperty->PropertyClass;

			if (PropertyClass == UMaterial::StaticClass())
			{
				return DrawTextureSlot(reinterpret_cast<UMaterial**>(ObjPtr), 0);
			}
			else if (PropertyClass == UFont::StaticClass())
			{
				DrawFontSlot(Object, reinterpret_cast<UFont**>(ObjPtr));
			}
			else if (PropertyClass == UStaticMesh::StaticClass())
			{
				DrawMeshSlot(Object, reinterpret_cast<UStaticMesh**>(ObjPtr));
			}
			else if (PropertyClass == UParticleSystem::StaticClass())
			{
				return DrawParticleSystemSlot(Object, reinterpret_cast<UParticleSystem**>(ObjPtr));
			}
			else if (PropertyClass == UTexture2D::StaticClass())
			{
				UTexture2D** TexturePtr = reinterpret_cast<UTexture2D**>(ObjPtr);
				return DrawTexturePicker(*TexturePtr, Label,
					[TexturePtr](UTexture2D* Picked) { *TexturePtr = Picked; });
			}
			return false;
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(&Property))
		{
			// USTRUCT: 구조체의 프로퍼티를 한 단계 들여 그린다. 컨테이너가 객체가 아니라 이 멤버의 주소다.
			const bool bUsePlainIndent = (GPropertyGridDepth == 0);
			if (bUsePlainIndent)
			{
				ImGui::Indent();
			}
			ImGui::PushID(Label);

			if (const FPropertyEditor::FStructDrawer* Drawer = GetStructDrawers().Find(StructProperty->Struct))
			{
				const bool bCustomChanged = (*Drawer)(Object, ValuePtr, *StructProperty->Struct, StructProperty->Meta, CustomFont);
				ImGui::PopID();
				if (bUsePlainIndent)
				{
					ImGui::Unindent();
				}
				return bCustomChanged;
			}

			TArray<const UStruct*> StructChain;
			for (const UStruct* Struct = StructProperty->Struct; Struct; Struct = Struct->GetSuperStruct())
			{
				StructChain.Add(Struct);
			}

			bool bChanged = false;
			for (auto It = StructChain.rbegin(); It != StructChain.rend(); ++It)
			{
				for (const FProperty* Member : (*It)->GetProperties())
				{
					if (!Member->HasAnyFlags(CPF_Edit))
					{
						continue;
					}
					const bool bReadOnly = Member->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					bChanged |= DrawPropertyRow(Object, ValuePtr, *Member, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}

			ImGui::PopID();
			if (bUsePlainIndent)
			{
				ImGui::Unindent();
			}
			return bChanged;
		}

		ImGui::TextDisabled("(Unsupported %s)", Property.GetClass()->GetName());
		return false;
	}

	// 프로퍼티 한 줄: 라벨과 값 위젯. 값이 바뀌면 true
	// Container는 프로퍼티를 가진 메모리 (UObject 멤버면 객체, USTRUCT 멤버면 그 구조체 값)
	bool DrawPropertyRow(UObject* Object, void* Container, const FProperty& Property, ImFont* CustomFont, const FPropertyMeta* MetaOverride)
	{
		void* ValuePtr = Property.ContainerPtrToValuePtr(Container);
		const FString Label = "##" + Property.Name;
		const char* DisplayName = Property.Name.c_str();

		if (GPropertyGridDepth > 0)
		{
			if (Property.IsA<FTransformProperty>())
			{
				if (Property.Name != "Transform" || GStructIndentLevel > 0)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel(DisplayName);
					ImGui::TableSetColumnIndex(1);
					++GStructIndentLevel;
					const bool bChanged = DrawPropertyValue(Object, Property, ValuePtr, Label.c_str(), CustomFont, MetaOverride);
					--GStructIndentLevel;
					return bChanged;
				}
				return DrawPropertyValue(Object, Property, ValuePtr, Label.c_str(), CustomFont, MetaOverride);
			}

			if (Property.IsA<FStructProperty>())
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				DrawPropertyGridLabel(DisplayName);
				ImGui::TableSetColumnIndex(1);
				++GStructIndentLevel;
				const bool bChanged = DrawPropertyValue(Object, Property, ValuePtr, Label.c_str(), CustomFont, MetaOverride);
				--GStructIndentLevel;
				return bChanged;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawPropertyGridLabel(DisplayName);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			return DrawPropertyValue(Object, Property, ValuePtr, Label.c_str(), CustomFont, MetaOverride);
		}

		// 테이블 바깥에서 단일 호출될 때의 폴백 레이아웃
		const float StartX = ImGui::GetCursorPosX();
		const float AvailableWidth = ImGui::GetContentRegionAvail().x;
		const float LabelWidth = AvailableWidth * GDetailsNameColumnRatio;

		const bool bInline = !Property.IsA<FTransformProperty>() && !Property.IsA<FStructProperty>() && AvailableWidth >= EditorStyle::Scale(260.0f) && ImGui::CalcTextSize(DisplayName).x < LabelWidth;

		ImGui::AlignTextToFramePadding();
		ImGui::TextWrapped("%s", DisplayName);

		if (bInline)
		{
			ImGui::SameLine();
			ImGui::SetCursorPosX(StartX + LabelWidth);
		}

		ImGui::SetNextItemWidth(-1.0f);

		return DrawPropertyValue(Object, Property, ValuePtr, Label.c_str(), CustomFont, MetaOverride);
	}

	// UClass에 등록된 프로퍼티 한 줄. 구조체 멤버 하나가 바뀌어도 객체에는 바깥 프로퍼티(구조체)가 바뀌었다고 알린다.
	// (UE는 FPropertyChangedEvent에 MemberProperty와 안쪽 Property를 따로 담는다)
	bool DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		if (!DrawPropertyRow(Object, Object, Property, CustomFont))
		{
			return false;
		}

		Object->PostEditChangeProperty(Property);
		return true;
	}

	// 클래스 계층을 따라 올라가며 각 단계의 프로퍼티를 표시
	bool DrawProperties(UObject* Object, ImFont* CustomFont)
	{
		if (!Object)
		{
			return false;
		}

		bool bChanged = false;

		TArray<UClass*> ClassChain;
		for (UClass* Class = Object->GetClass(); Class; Class = Class->Super)
		{
			ClassChain.Add(Class);
		}

		// 기반 클래스부터 표시
		for (auto It = ClassChain.rbegin(); It != ClassChain.rend(); ++It)
		{
			UClass* Class = *It;

			// 노출할 프로퍼티가 하나도 없으면 헤더도 그리지 않는다
			bool bHasVisibleProperty = false;
			for (const FProperty* Property : Class->GetProperties())
			{
				if (Property->HasAnyFlags(CPF_Edit))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty)
			{
				continue;
			}

			ImGui::PushID(Class->Name.c_str());
			if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (BeginPropertyGridTable("CategoryProperties"))
				{
					for (const FProperty* Property : Class->GetProperties())
					{
						if (!Property->HasAnyFlags(CPF_Edit))
						{
							continue;
						}

						const bool bReadOnly = Property->HasAnyFlags(CPF_EditConst);
						if (bReadOnly) ImGui::BeginDisabled();
						bChanged |= DrawProperty(Object, *Property, CustomFont);
						if (bReadOnly) ImGui::EndDisabled();
					}
					EndPropertyGridTable();
				}
			}
			ImGui::PopID();
		}

		return bChanged;
	}

	void DrawActorHeader(AActor* Actor, UActorComponent*& OutSelectedComponent, FEditorUndoHistory* UndoHistory)
	{
		if (!Actor) { return; }

		static char ComponentNameInputBuf[256] = {};

		// UE 디테일 패널 머리: 왼쪽에 액터 이름, 오른쪽 끝에 적색 + 가 붙은 "Add" 버튼
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(Actor->GetName().c_str());
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("UUID: %u", Actor->GetUUID());

		const char* AddLabel = "Add";
		const float PlusSize = EditorStyle::Scale(10.0f);
		const float PlusGap = EditorStyle::Scale(6.0f);
		const ImVec2 FramePadding = ImGui::GetStyle().FramePadding;
		const float AddWidth = FramePadding.x * 2.0f + PlusSize + PlusGap + ImGui::CalcTextSize(AddLabel).x + EditorStyle::Scale(4.0f);
		ImGui::SameLine();
		ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - AddWidth));
		ImGui::PushStyleColor(ImGuiCol_Button, EditorColor::BgElevated);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, EditorColor::BgAccentHover);
		const bool bAddClicked = ImGui::Button("##AddComponent", { AddWidth, ImGui::GetFrameHeight() });
		ImGui::PopStyleColor(2);
		{
			const ImVec2 Min = ImGui::GetItemRectMin(), Max = ImGui::GetItemRectMax();
			const float CenterY = (Min.y + Max.y) * 0.5f;
			const float PlusX = Min.x + FramePadding.x + PlusSize * 0.5f;
			const float Thick = std::max(2.0f, EditorStyle::Scale(2.0f));
			const ImU32 Crimson = EditorColor::AccentBright;
			ImDrawList* DrawList = ImGui::GetWindowDrawList();
			DrawList->AddRectFilled({ PlusX - PlusSize * 0.5f, CenterY - Thick * 0.5f }, { PlusX + PlusSize * 0.5f, CenterY + Thick * 0.5f }, Crimson);
			DrawList->AddRectFilled({ PlusX - Thick * 0.5f, CenterY - PlusSize * 0.5f }, { PlusX + Thick * 0.5f, CenterY + PlusSize * 0.5f }, Crimson);
			DrawList->AddText({ PlusX + PlusSize * 0.5f + PlusGap, CenterY - ImGui::GetTextLineHeight() * 0.5f },
				ImGui::GetColorU32(ImGuiCol_Text), AddLabel);
		}
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add a component to this actor");
		if (bAddClicked)
		{
			ImGui::OpenPopup("AddComponentPopup");
		}

		const auto AddComponentAnchor = EditorStyle::LastItemAnchor();

		if (EditorStyle::BeginButtonPopup("AddComponentPopup", AddComponentAnchor, EditorStyle::Scale(320.0f)))
		{
			ImGui::Text("Component Name:");
			ImGui::SameLine();
			ImGui::InputText("##Component Name", ComponentNameInputBuf, sizeof(ComponentNameInputBuf));
			TArray<UClass*> Classes;
			GetDerivedClasses(UActorComponent::StaticClass(), Classes);
			std::sort(Classes.begin(), Classes.end(), [](const UClass* A, const UClass* B) { return A->Name < B->Name; });
            const auto Category = [](const UClass* Class) -> const char*
            {
                const FString& Name = Class->Name;
                if (Name.find("Light") != FString::npos || Name.find("Fog") != FString::npos) return "Lighting";
                if (Name.find("Camera") != FString::npos || Name.find("SpringArm") != FString::npos) return "Camera";
                if (Name.find("Audio") != FString::npos) return "Audio";
                if (Name.find("Movement") != FString::npos) return "Movement";
                if (Name.find("Collision") != FString::npos || Name.find("Shape") != FString::npos ||
                    Name.find("Box") != FString::npos || Name.find("Sphere") != FString::npos || Name.find("Capsule") != FString::npos) return "Collision";
                if (Name.find("Mesh") != FString::npos || Name.find("Render") != FString::npos ||
                    Name.find("Particle") != FString::npos || Name.find("Decal") != FString::npos || Name.find("Billboard") != FString::npos) return "Rendering";
                if (Name.find("Script") != FString::npos) return "Scripting";
                return "Common";
            };
            const char* Categories[] = { "Common", "Rendering", "Lighting", "Camera", "Collision", "Movement", "Audio", "Scripting" };
            for (const char* Group : Categories)
            {
                bool HasClasses = false;
                for (UClass* Class : Classes) if (strcmp(Category(Class), Group) == 0) HasClasses = true;
                if (!HasClasses || !ImGui::BeginMenu(Group)) continue;
                for (UClass* Class : Classes)
                {
                    if (strcmp(Category(Class), Group) != 0) continue;
                    if (ImGui::MenuItem(Class->Name.c_str()))
                    {
                        UActorComponent* Added = Actor->AddComponent(Class, FString(ComponentNameInputBuf));
                        OutSelectedComponent = Added;
                        if (Added && UndoHistory && UndoHistory->CanRecord())
                        {
                            TArray<uint8> CompBytes;
                            EditorUndoUtils::CaptureObjectBytes(Added, CompBytes);
                            FName ParentCompName;
                            if (USceneComponent* SceneComp = Cast<USceneComponent>(Added))
                            {
                                if (USceneComponent* Parent = SceneComp->GetAttachParent(); Parent && Parent->GetOwner() == Actor)
                                {
                                    ParentCompName = Parent->GetFName();
                                }
                            }
                            UndoHistory->Push(EditorUndoUtils::MakeComponentAddRemoveCommand(
                                "Add Component",
                                Actor->GetFName(),
                                Added->GetFName(),
                                Class->Name,
                                ParentCompName,
                                {},
                                std::move(CompBytes),
                                true));
                        }
                        ComponentNameInputBuf[0] = '\0';
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndMenu();
            }
			ImGui::EndPopup();
		}
	}

	// ---- 다중 선택 공통 프로퍼티 편집 도우미 ----
	TArray<UClass*> GetCommonClassChain(const TArray<UObject*>& Objects)
	{
		TArray<UClass*> Common;
		if (Objects.IsEmpty() || !Objects[0])
		{
			return Common;
		}

		auto BuildChain = [](UClass* Leaf) -> TArray<UClass*>
		{
			TArray<UClass*> Temp;
			for (UClass* C = Leaf; C; C = C->Super)
			{
				Temp.Add(C);
			}
			TArray<UClass*> Result;
			for (auto It = Temp.rbegin(); It != Temp.rend(); ++It)
			{
				Result.Add(*It);
			}
			return Result;
		};

		Common = BuildChain(Objects[0]->GetClass());
		for (int32 i = 1; i < Objects.Num(); ++i)
		{
			if (!Objects[i])
			{
				Common.Empty();
				return Common;
			}
			const TArray<UClass*> Chain = BuildChain(Objects[i]->GetClass());
			int32 MatchLen = 0;
			const int32 Limit = (std::min)(Common.Num(), Chain.Num());
			while (MatchLen < Limit && Common[MatchLen] == Chain[MatchLen])
			{
				++MatchLen;
			}
			while (Common.Num() > MatchLen)
			{
				Common.RemoveAt(Common.Num() - 1);
			}
		}
		return Common;
	}

	bool DrawMultiAxisControl(
		const FString& Label,
		float& Value,
		bool bMixed,
		float Speed,
		float MinValue,
		float MaxValue,
		float ResetValue,
		FVector4 Color)
	{
		bool bChanged = false;
		const float LineHeight = ImGui::GetFrameHeight();
		const ImVec4 BtnColor(Color.X, Color.Y, Color.Z, Color.W);
		const ImVec2 ButtonSize = { LineHeight + EditorStyle::Scale(2.0f), LineHeight };

		ImGui::PushStyleColor(ImGuiCol_Button, BtnColor);
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{ BtnColor.x + 0.1f, BtnColor.y + 0.1f, BtnColor.z + 0.1f, 1.0f });
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4{ BtnColor.x - 0.1f, BtnColor.y - 0.1f, BtnColor.z - 0.1f, 1.0f });
		if (ImGui::Button(Label.c_str(), ButtonSize))
		{
			Value = ResetValue;
			bChanged = true;
		}
		ImGui::PopStyleColor(3);

		ImGui::SameLine();
		const FString DragId = "##" + Label;
		bChanged |= ImGui::DragFloat(DragId.c_str(), &Value, Speed, MinValue, MaxValue, bMixed ? "---" : "%.2f");
		return bChanged;
	}

	bool DrawMultiVector3Controller(
		const FString& Label,
		float Values[3],
		const bool Mixed[3],
		bool OutAxisChanged[3],
		float ResetValue,
		float ColumnWidth)
	{
		(void)ColumnWidth;
		OutAxisChanged[0] = OutAxisChanged[1] = OutAxisChanged[2] = false;
		ImGuiIO& IO = ImGui::GetIO();
		auto BoldFont = IO.FontDefault ? IO.FontDefault : IO.Fonts->Fonts[0];
		const bool bUseOwnTable = (GPropertyGridDepth == 0);

		bool bAnyChanged = false;
		ImGui::PushID(Label.c_str());
		if (!bUseOwnTable || BeginPropertyGridTable(Label.c_str()))
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawPropertyGridLabel(Label.c_str());

			ImGui::TableSetColumnIndex(1);
			const float Gap = EditorStyle::Scale(2.0f);
			const float ButtonWidth = ImGui::GetFrameHeight() + EditorStyle::Scale(2.0f);
			const float Available = ImGui::GetContentRegionAvail().x;
			const bool bStackAxes = Available < EditorStyle::Scale(165.0f);
			const float ValueWidth = bStackAxes
				? ImMax(1.0f, Available - ButtonWidth - Gap)
				: ImMax(3.0f, Available - 3.0f * ButtonWidth - 5.0f * Gap);
			ImGui::PushMultiItemsWidths(bStackAxes ? 1 : 3, ValueWidth);
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{ Gap, ImGui::GetStyle().ItemSpacing.y });
			ImGui::PushFont(BoldFont);

			OutAxisChanged[0] = DrawMultiAxisControl("X", Values[0], Mixed[0], 0.1f, 0.0f, 0.0f, ResetValue, { 0.8f, 0.1f, 0.1f, 1.0f });
			ImGui::PopItemWidth();

			if (bStackAxes) ImGui::PushItemWidth(ValueWidth); else ImGui::SameLine();
			OutAxisChanged[1] = DrawMultiAxisControl("Y", Values[1], Mixed[1], 0.1f, 0.0f, 0.0f, ResetValue, { 0.1f, 0.8f, 0.1f, 1.0f });
			ImGui::PopItemWidth();

			if (bStackAxes) ImGui::PushItemWidth(ValueWidth); else ImGui::SameLine();
			OutAxisChanged[2] = DrawMultiAxisControl("Z", Values[2], Mixed[2], 0.1f, 0.0f, 0.0f, ResetValue, { 0.1f, 0.1f, 0.8f, 1.0f });
			ImGui::PopItemWidth();

			bAnyChanged = OutAxisChanged[0] || OutAxisChanged[1] || OutAxisChanged[2];

			ImGui::PopFont();
			ImGui::PopStyleVar();
			if (bUseOwnTable)
			{
				EndPropertyGridTable();
			}
		}
		ImGui::PopID();
		return bAnyChanged;
	}

	bool DrawMultiPropertyRow(
		const TArray<UObject*>& Objects,
		const TArray<void*>& Containers,
		const FProperty& Property,
		ImFont* CustomFont);

	bool DrawMultiPropertyValue(
		const TArray<UObject*>& Objects,
		const TArray<void*>& ValuePtrs,
		const FProperty& Property,
		const char* Label,
		ImFont* CustomFont)
	{
		if (Objects.IsEmpty() || ValuePtrs.Num() != Objects.Num())
		{
			return false;
		}
		constexpr float FloatEps = 1e-4f;

		if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(&Property))
		{
			const float First = FloatProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				if (std::abs(FloatProp->GetPropertyValue(ValuePtrs[i]) - First) > FloatEps)
				{
					bMixed = true;
					break;
				}
			}
			float Val = First;
			const FDragSettings Drag = GetDragSettings(FloatProp->Meta, 0.1f);
			if (ImGui::DragFloat(Label, &Val, Drag.Speed, Drag.Min, Drag.Max, bMixed ? "Multiple Values" : "%.3f"))
			{
				ClampToMeta(&Val, 1, FloatProp->Meta);
				for (void* Ptr : ValuePtrs)
				{
					FloatProp->GetPropertyValue(Ptr) = Val;
				}
				return true;
			}
			return false;
		}

		if (const FIntProperty* IntProp = CastField<FIntProperty>(&Property))
		{
			const int32 First = IntProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				if (IntProp->GetPropertyValue(ValuePtrs[i]) != First)
				{
					bMixed = true;
					break;
				}
			}
			int32 Val = First;
			const FPropertyMeta& Meta = IntProp->Meta;
			const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
			if (ImGui::DragInt(Label, &Val, Drag.Speed, ToIntBound(Drag.Min), ToIntBound(Drag.Max), bMixed ? "Multiple Values" : "%d"))
			{
				if (Meta.HasClamp())
				{
					Val = ToIntBound(std::clamp(static_cast<float>(Val), Meta.ClampMin, Meta.ClampMax));
				}
				for (void* Ptr : ValuePtrs)
				{
					IntProp->GetPropertyValue(Ptr) = Val;
				}
				return true;
			}
			return false;
		}

		if (const FUInt32Property* UIntProp = CastField<FUInt32Property>(&Property))
		{
			const uint32 First = UIntProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				if (UIntProp->GetPropertyValue(ValuePtrs[i]) != First)
				{
					bMixed = true;
					break;
				}
			}
			uint32 Val = First;
			const FPropertyMeta& Meta = UIntProp->Meta;
			const FDragSettings Drag = GetDragSettings(Meta, 1.0f);
			const uint32 Min = ToUIntBound(Drag.Min);
			const uint32 Max = Drag.Min < Drag.Max ? ToUIntBound(Drag.Max) : UINT_MAX;
			if (ImGui::DragScalar(Label, ImGuiDataType_U32, &Val, Drag.Speed, &Min, &Max, bMixed ? "Multiple Values" : "%u"))
			{
				if (Meta.HasClamp())
				{
					Val = ToUIntBound(std::clamp(static_cast<float>(Val), Meta.ClampMin, Meta.ClampMax));
				}
				for (void* Ptr : ValuePtrs)
				{
					UIntProp->GetPropertyValue(Ptr) = Val;
				}
				return true;
			}
			return false;
		}

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(&Property))
		{
			bool bAllTrue = true;
			bool bAllFalse = true;
			for (void* Ptr : ValuePtrs)
			{
				const bool V = BoolProp->GetPropertyValue(Ptr);
				bAllTrue &= V;
				bAllFalse &= !V;
			}
			const bool bMixed = !(bAllTrue || bAllFalse);
			bool DisplayVal = bAllTrue;
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, bMixed);
			const bool bClicked = ImGui::Checkbox(Label, &DisplayVal);
			ImGui::PopItemFlag();
			if (bClicked)
			{
				const bool NewVal = !bAllTrue;
				for (void* Ptr : ValuePtrs)
				{
					BoolProp->GetPropertyValue(Ptr) = NewVal;
				}
				return true;
			}
			return false;
		}

		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(&Property))
		{
			const FEnumInfo* EnumInfo = EnumProp->Enum;
			if (!EnumInfo)
			{
				ImGui::TextDisabled("(No enum info)");
				return false;
			}
			const int64 First = EnumProp->GetValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				if (EnumProp->GetValue(ValuePtrs[i]) != First)
				{
					bMixed = true;
					break;
				}
			}
			bool bChanged = false;
			const FString Preview = bMixed ? "Multiple Values" : EnumInfo->ToName(First);
			if (ImGui::BeginCombo(Label, Preview.c_str()))
			{
				for (const FEnumEntry& Entry : EnumInfo->Entries)
				{
					if (Entry.bHidden) continue;
					const bool bSelected = !bMixed && Entry.Value == First;
					if (ImGui::Selectable(Entry.Name.c_str(), bSelected) && (bMixed || !bSelected))
					{
						for (void* Ptr : ValuePtrs)
						{
							EnumProp->SetValue(Ptr, Entry.Value);
						}
						bChanged = true;
					}
					if (bSelected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			return bChanged;
		}

		if (const FVectorProperty* VecProp = CastField<FVectorProperty>(&Property))
		{
			const FVector& First = VecProp->GetPropertyValue(ValuePtrs[0]);
			float Vals[3] = { First.X, First.Y, First.Z };
			bool Mixed[3] = { false, false, false };
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				const FVector& V = VecProp->GetPropertyValue(ValuePtrs[i]);
				if (std::abs(V.X - First.X) > FloatEps) Mixed[0] = true;
				if (std::abs(V.Y - First.Y) > FloatEps) Mixed[1] = true;
				if (std::abs(V.Z - First.Z) > FloatEps) Mixed[2] = true;
			}
			bool AxisChanged[3] = {};
			if (DrawMultiVector3Controller(Property.Name, Vals, Mixed, AxisChanged, 0.0f, 55.0f))
			{
				for (void* Ptr : ValuePtrs)
				{
					FVector& V = VecProp->GetPropertyValue(Ptr);
					if (AxisChanged[0]) V.X = Vals[0];
					if (AxisChanged[1]) V.Y = Vals[1];
					if (AxisChanged[2]) V.Z = Vals[2];
					ClampToMeta(V.V, 3, VecProp->Meta);
				}
				return true;
			}
			return false;
		}

		if (const FRotatorProperty* RotProp = CastField<FRotatorProperty>(&Property))
		{
			const FRotator& First = RotProp->GetPropertyValue(ValuePtrs[0]);
			float Vals[3] = { First.Roll, First.Pitch, First.Yaw };
			bool Mixed[3] = { false, false, false };
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				const FRotator& R = RotProp->GetPropertyValue(ValuePtrs[i]);
				if (std::abs(R.Roll - First.Roll) > FloatEps) Mixed[0] = true;
				if (std::abs(R.Pitch - First.Pitch) > FloatEps) Mixed[1] = true;
				if (std::abs(R.Yaw - First.Yaw) > FloatEps) Mixed[2] = true;
			}
			bool AxisChanged[3] = {};
			if (DrawMultiVector3Controller(Property.Name, Vals, Mixed, AxisChanged, 0.0f, 55.0f))
			{
				ClampToMeta(Vals, 3, RotProp->Meta);
				for (void* Ptr : ValuePtrs)
				{
					FRotator& R = RotProp->GetPropertyValue(Ptr);
					if (AxisChanged[0]) R.Roll = Vals[0];
					if (AxisChanged[1]) R.Pitch = Vals[1];
					if (AxisChanged[2]) R.Yaw = Vals[2];
				}
				return true;
			}
			return false;
		}

		if (const FVector4Property* Vec4Prop = CastField<FVector4Property>(&Property))
		{
			const FVector4& First = Vec4Prop->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				const FVector4& V = Vec4Prop->GetPropertyValue(ValuePtrs[i]);
				if (std::abs(V.X - First.X) > FloatEps || std::abs(V.Y - First.Y) > FloatEps ||
					std::abs(V.Z - First.Z) > FloatEps || std::abs(V.W - First.W) > FloatEps)
				{
					bMixed = true;
					break;
				}
			}
			FVector4 Val = First;
			const FDragSettings Drag = GetDragSettings(Vec4Prop->Meta, 0.1f);
			if (ImGui::DragFloat4(Label, &Val.X, Drag.Speed, Drag.Min, Drag.Max, bMixed ? "Multiple Values" : "%.3f"))
			{
				ClampToMeta(&Val.X, 4, Vec4Prop->Meta);
				for (void* Ptr : ValuePtrs)
				{
					Vec4Prop->GetPropertyValue(Ptr) = Val;
				}
				return true;
			}
			return false;
		}

		if (const FColorProperty* ColorProp = CastField<FColorProperty>(&Property))
		{
			const FColor& First = ColorProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				const FColor& C = ColorProp->GetPropertyValue(ValuePtrs[i]);
				if (C.R != First.R || C.G != First.G || C.B != First.B || C.A != First.A)
				{
					bMixed = true;
					break;
				}
			}
			float V[4] = { First.R / 255.f, First.G / 255.f, First.B / 255.f, First.A / 255.f };
			const bool bChanged = ImGui::ColorEdit4(Label, V);
			if (bMixed) ImGui::TextDisabled("(Multiple Values)");
			if (bChanged)
			{
				const FColor NewC(
					uint8(V[0] * 255.f + 0.5f), uint8(V[1] * 255.f + 0.5f),
					uint8(V[2] * 255.f + 0.5f), uint8(V[3] * 255.f + 0.5f));
				for (void* Ptr : ValuePtrs)
				{
					ColorProp->GetPropertyValue(Ptr) = NewC;
				}
				return true;
			}
			return false;
		}

		if (const FLinearColorProperty* LinColorProp = CastField<FLinearColorProperty>(&Property))
		{
			const FLinearColor& First = LinColorProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				const FLinearColor& C = LinColorProp->GetPropertyValue(ValuePtrs[i]);
				if (std::abs(C.RGBA[0] - First.RGBA[0]) > FloatEps || std::abs(C.RGBA[1] - First.RGBA[1]) > FloatEps ||
					std::abs(C.RGBA[2] - First.RGBA[2]) > FloatEps || std::abs(C.RGBA[3] - First.RGBA[3]) > FloatEps)
				{
					bMixed = true;
					break;
				}
			}
			FLinearColor Val = First;
			const bool bChanged = ImGui::ColorEdit4(Label, Val.RGBA);
			if (bMixed) ImGui::TextDisabled("(Multiple Values)");
			if (bChanged)
			{
				for (void* Ptr : ValuePtrs)
				{
					LinColorProp->GetPropertyValue(Ptr) = Val;
				}
				return true;
			}
			return false;
		}

		if (const FStrProperty* StrProp = CastField<FStrProperty>(&Property))
		{
			const FString& First = StrProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				if (StrProp->GetPropertyValue(ValuePtrs[i]) != First)
				{
					bMixed = true;
					break;
				}
			}
			char Buffer[256] = {};
			if (!bMixed)
			{
				strncpy_s(Buffer, First.c_str(), sizeof(Buffer) - 1);
			}
			if (CustomFont) ImGui::PushFont(CustomFont);
			const ImGuiInputTextFlags TextFlags = bMixed ? ImGuiInputTextFlags_EnterReturnsTrue : ImGuiInputTextFlags_None;
			const bool bChanged = ImGui::InputTextWithHint(Label, bMixed ? "Multiple Values (Enter)" : "", Buffer, sizeof(Buffer), TextFlags);
			if (CustomFont) ImGui::PopFont();
			if (bChanged)
			{
				const FString NewStr(Buffer);
				for (void* Ptr : ValuePtrs)
				{
					StrProp->GetPropertyValue(Ptr) = NewStr;
				}
				return true;
			}
			return false;
		}

		if (const FTransformProperty* TransformProp = CastField<FTransformProperty>(&Property))
		{
			static TMap<const void*, FRotationConversionCache> MultiRotationCaches;
			const FTransform& First = TransformProp->GetPropertyValue(ValuePtrs[0]);
			const FRotator FirstRot = MultiRotationCaches[ValuePtrs[0]].QuatToRotator(First.Rotation);

			float Loc[3] = { First.Location.X, First.Location.Y, First.Location.Z };
			float Rot[3] = { FirstRot.Roll, FirstRot.Pitch, FirstRot.Yaw };
			float Scale[3] = { First.Scale.X, First.Scale.Y, First.Scale.Z };
			bool LocMixed[3] = { false, false, false };
			bool RotMixed[3] = { false, false, false };
			bool ScaleMixed[3] = { false, false, false };

			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				const FTransform& T = TransformProp->GetPropertyValue(ValuePtrs[i]);
				const FRotator R = MultiRotationCaches[ValuePtrs[i]].QuatToRotator(T.Rotation);
				if (std::abs(T.Location.X - First.Location.X) > FloatEps) LocMixed[0] = true;
				if (std::abs(T.Location.Y - First.Location.Y) > FloatEps) LocMixed[1] = true;
				if (std::abs(T.Location.Z - First.Location.Z) > FloatEps) LocMixed[2] = true;

				if (std::abs(R.Roll - FirstRot.Roll) > FloatEps) RotMixed[0] = true;
				if (std::abs(R.Pitch - FirstRot.Pitch) > FloatEps) RotMixed[1] = true;
				if (std::abs(R.Yaw - FirstRot.Yaw) > FloatEps) RotMixed[2] = true;

				if (std::abs(T.Scale.X - First.Scale.X) > FloatEps) ScaleMixed[0] = true;
				if (std::abs(T.Scale.Y - First.Scale.Y) > FloatEps) ScaleMixed[1] = true;
				if (std::abs(T.Scale.Z - First.Scale.Z) > FloatEps) ScaleMixed[2] = true;
			}

			ImGui::PushID(Property.Name.c_str());
			bool bChanged = false;
			bool LocChanged[3] = {};
			if (DrawMultiVector3Controller("Location", Loc, LocMixed, LocChanged, 0.0f, 55.0f))
			{
				for (void* Ptr : ValuePtrs)
				{
					FTransform& T = TransformProp->GetPropertyValue(Ptr);
					if (LocChanged[0]) T.Location.X = Loc[0];
					if (LocChanged[1]) T.Location.Y = Loc[1];
					if (LocChanged[2]) T.Location.Z = Loc[2];
				}
				bChanged = true;
			}

			bool RotChanged[3] = {};
			if (DrawMultiVector3Controller("Rotation", Rot, RotMixed, RotChanged, 0.0f, 55.0f))
			{
				for (void* Ptr : ValuePtrs)
				{
					FTransform& T = TransformProp->GetPropertyValue(Ptr);
					FRotationConversionCache& Cache = MultiRotationCaches[Ptr];
					FRotator Euler = Cache.QuatToRotator(T.Rotation);
					if (RotChanged[0]) Euler.Roll = Rot[0];
					if (RotChanged[1]) Euler.Pitch = Rot[1];
					if (RotChanged[2]) Euler.Yaw = Rot[2];
					T.Rotation = Cache.RotatorToQuat(Euler);
				}
				bChanged = true;
			}

			bool ScaleChanged[3] = {};
			if (DrawMultiVector3Controller("Scale", Scale, ScaleMixed, ScaleChanged, 1.0f, 55.0f))
			{
				for (void* Ptr : ValuePtrs)
				{
					FTransform& T = TransformProp->GetPropertyValue(Ptr);
					if (ScaleChanged[0]) T.Scale.X = Scale[0];
					if (ScaleChanged[1]) T.Scale.Y = Scale[1];
					if (ScaleChanged[2]) T.Scale.Z = Scale[2];
				}
				bChanged = true;
			}
			ImGui::PopID();
			return bChanged;
		}

		if (const FObjectProperty* ObjectProp = CastField<FObjectProperty>(&Property))
		{
			UObject* First = ObjectProp->GetPropertyValue(ValuePtrs[0]);
			bool bMixed = false;
			for (int32 i = 1; i < ValuePtrs.Num(); ++i)
			{
				if (ObjectProp->GetPropertyValue(ValuePtrs[i]) != First)
				{
					bMixed = true;
					break;
				}
			}
			const UClass* PropertyClass = ObjectProp->PropertyClass;
			if (PropertyClass == UStaticMesh::StaticClass())
			{
				UStaticMesh* Current = bMixed ? nullptr : Cast<UStaticMesh>(First);
				const FString CurrentPath = Current ? Current->GetPath() : "";
				const FString Preview = bMixed ? "Multiple Values" : (Current ? GetAssetDisplayName(CurrentPath) : "None");
				ImGui::SetNextItemWidth(-1.0f);
				bool bPicked = false;
				if (ImGui::BeginCombo("##MultiStaticMesh", Preview.c_str()))
				{
					auto ApplyMesh = [&](UStaticMesh* NewMesh)
					{
						for (int32 i = 0; i < Objects.Num(); ++i)
						{
							if (UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(Objects[i]))
								MeshComp->SetStaticMesh(NewMesh);
							else
								ObjectProp->GetPropertyValue(ValuePtrs[i]) = NewMesh;
						}
						bPicked = true;
					};
					if (ImGui::Selectable("None", !bMixed && Current == nullptr) && (bMixed || Current != nullptr))
					{
						ApplyMesh(nullptr);
					}
					for (TObjectIterator<UStaticMesh> Itr; Itr; ++Itr)
					{
						const bool bSelected = !bMixed && (*Itr == Current);
						const FString ItemPath = Itr->GetPath();
						ImGui::PushID(*Itr);
						if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), bSelected))
						{
							ApplyMesh(*Itr);
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
						ImGui::PopID();
					}
					for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::StaticMesh))
					{
						ImGui::PushID(ItemPath.c_str());
						if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
						{
							if (UStaticMesh* Mesh = UAssetManager::GetAssetByPath<UStaticMesh>(ItemPath))
								ApplyMesh(Mesh);
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				return bPicked;
			}
			else if (PropertyClass == UFont::StaticClass())
			{
				UFont* Current = bMixed ? nullptr : Cast<UFont>(First);
				const FString Preview = bMixed ? "Multiple Values" : (Current ? GetAssetDisplayName(Current->GetPath()) : "None");
				ImGui::SetNextItemWidth(-1.0f);
				bool bPicked = false;
				if (ImGui::BeginCombo("##MultiFont", Preview.c_str()))
				{
					auto ApplyFont = [&](UFont* NewFont)
					{
						for (int32 i = 0; i < Objects.Num(); ++i)
						{
							if (UTextRenderComponent* TextComp = Cast<UTextRenderComponent>(Objects[i]))
								TextComp->SetFont(NewFont);
							else
								ObjectProp->GetPropertyValue(ValuePtrs[i]) = NewFont;
						}
						bPicked = true;
					};
					for (TObjectIterator<UFont> Itr; Itr; ++Itr)
					{
						const bool bSelected = !bMixed && (*Itr == Current);
						const FString ItemPath = Itr->GetPath();
						ImGui::PushID(*Itr);
						if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), bSelected))
							ApplyFont(*Itr);
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
						ImGui::PopID();
					}
					for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::Font))
					{
						ImGui::PushID(ItemPath.c_str());
						if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
						{
							if (UFont* Font = UAssetManager::GetAssetByPath<UFont>(ItemPath))
								ApplyFont(Font);
						}
						if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
						ImGui::PopID();
					}
					ImGui::EndCombo();
				}
				return bPicked;
			}
			else if (PropertyClass == UParticleSystem::StaticClass())
			{
				return DrawParticleSystemPicker(Cast<UParticleSystem>(First), bMixed, "##MultiParticleSystem",
					[&](UParticleSystem* Picked)
					{
						for (int32 i = 0; i < Objects.Num(); ++i)
						{
							ApplyParticleTemplate(Objects[i],
								reinterpret_cast<UParticleSystem**>(&ObjectProp->GetPropertyValue(ValuePtrs[i])), Picked);
						}
					});
			}
			else if (PropertyClass == UTexture2D::StaticClass())
			{
				UTexture2D* Current = bMixed ? nullptr : Cast<UTexture2D>(First);
				return DrawTexturePicker(Current, Label,
					[&](UTexture2D* Picked)
					{
						for (void* Ptr : ValuePtrs)
						{
							ObjectProp->GetPropertyValue(Ptr) = Picked;
						}
					});
			}
			else
			{
				ImGui::TextDisabled(bMixed ? "Multiple Values" : "(Object)");
			}
			return false;
		}

		if (const FStructProperty* StructProp = CastField<FStructProperty>(&Property))
		{
			const bool bUsePlainIndent = (GPropertyGridDepth == 0);
			if (bUsePlainIndent)
			{
				ImGui::Indent();
			}
			ImGui::PushID(Label);

			TArray<const UStruct*> StructChain;
			for (const UStruct* Struct = StructProp->Struct; Struct; Struct = Struct->GetSuperStruct())
			{
				StructChain.Add(Struct);
			}

			bool bChanged = false;
			for (auto It = StructChain.rbegin(); It != StructChain.rend(); ++It)
			{
				for (const FProperty* Member : (*It)->GetProperties())
				{
					if (!Member->HasAnyFlags(CPF_Edit)) continue;
					const bool bReadOnly = Member->HasAnyFlags(CPF_EditConst);
					if (bReadOnly) ImGui::BeginDisabled();
					bChanged |= DrawMultiPropertyRow(Objects, ValuePtrs, *Member, CustomFont);
					if (bReadOnly) ImGui::EndDisabled();
				}
			}

			ImGui::PopID();
			if (bUsePlainIndent)
			{
				ImGui::Unindent();
			}
			return bChanged;
		}

		ImGui::TextDisabled("(Unsupported %s)", Property.GetClass()->GetName());
		return false;
	}

	bool DrawMultiPropertyRow(
		const TArray<UObject*>& Objects,
		const TArray<void*>& Containers,
		const FProperty& Property,
		ImFont* CustomFont)
	{
		TArray<void*> ValuePtrs;
		ValuePtrs.Reserve(Containers.Num());
		for (void* Container : Containers)
		{
			ValuePtrs.Add(Property.ContainerPtrToValuePtr(Container));
		}

		const FString Label = "##" + Property.Name;
		const char* DisplayName = Property.Name.c_str();
		const bool bUsesFullWidthController = Property.IsA<FVectorProperty>() || Property.IsA<FRotatorProperty>();

		if (GPropertyGridDepth > 0)
		{
			if (bUsesFullWidthController)
			{
				return DrawMultiPropertyValue(Objects, ValuePtrs, Property, Label.c_str(), CustomFont);
			}
			if (Property.IsA<FTransformProperty>())
			{
				if (Property.Name != "Transform" || GStructIndentLevel > 0)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel(DisplayName);
					ImGui::TableSetColumnIndex(1);
					++GStructIndentLevel;
					const bool bChanged = DrawMultiPropertyValue(Objects, ValuePtrs, Property, Label.c_str(), CustomFont);
					--GStructIndentLevel;
					return bChanged;
				}
				return DrawMultiPropertyValue(Objects, ValuePtrs, Property, Label.c_str(), CustomFont);
			}
			if (Property.IsA<FStructProperty>())
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				DrawPropertyGridLabel(DisplayName);
				ImGui::TableSetColumnIndex(1);
				++GStructIndentLevel;
				const bool bChanged = DrawMultiPropertyValue(Objects, ValuePtrs, Property, Label.c_str(), CustomFont);
				--GStructIndentLevel;
				return bChanged;
			}

			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			DrawPropertyGridLabel(DisplayName);
			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-1.0f);
			return DrawMultiPropertyValue(Objects, ValuePtrs, Property, Label.c_str(), CustomFont);
		}

		if (!bUsesFullWidthController)
		{
			const float StartX = ImGui::GetCursorPosX();
			const float AvailableWidth = ImGui::GetContentRegionAvail().x;
			const float LabelWidth = AvailableWidth * GDetailsNameColumnRatio;
			const bool bInline = !Property.IsA<FTransformProperty>() && !Property.IsA<FStructProperty>()
				&& AvailableWidth >= EditorStyle::Scale(260.0f) && ImGui::CalcTextSize(DisplayName).x < LabelWidth;

			ImGui::AlignTextToFramePadding();
			ImGui::TextWrapped("%s", DisplayName);
			if (bInline)
			{
				ImGui::SameLine();
				ImGui::SetCursorPosX(StartX + LabelWidth);
			}
			ImGui::SetNextItemWidth(-1.0f);
		}

		return DrawMultiPropertyValue(Objects, ValuePtrs, Property, Label.c_str(), CustomFont);
	}

	void DrawMultiProperties(const TArray<UObject*>& Objects, ImFont* CustomFont)
	{
		if (Objects.IsEmpty())
		{
			return;
		}

		const TArray<UClass*> CommonChain = GetCommonClassChain(Objects);
		for (UClass* Class : CommonChain)
		{
			bool bHasVisibleProperty = false;
			for (const FProperty* Property : Class->GetProperties())
			{
				if (Property->HasAnyFlags(CPF_Edit))
				{
					bHasVisibleProperty = true;
					break;
				}
			}
			if (!bHasVisibleProperty) continue;

			ImGui::PushID(Class->Name.c_str());
			if (ImGui::CollapsingHeader(Class->Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (BeginPropertyGridTable("MultiCategoryProperties"))
				{
					TArray<void*> Containers;
					Containers.Reserve(Objects.Num());
					for (UObject* Obj : Objects)
					{
						Containers.Add(Obj);
					}

					for (const FProperty* Property : Class->GetProperties())
					{
						if (!Property->HasAnyFlags(CPF_Edit)) continue;
						const bool bReadOnly = Property->HasAnyFlags(CPF_EditConst);
						if (bReadOnly) ImGui::BeginDisabled();
						if (DrawMultiPropertyRow(Objects, Containers, *Property, CustomFont))
						{
							for (UObject* Obj : Objects)
							{
								Obj->PostEditChangeProperty(*Property);
							}
						}
						if (bReadOnly) ImGui::EndDisabled();
					}
					EndPropertyGridTable();
				}
			}
			ImGui::PopID();
		}
	}

	void DrawMultiSkeletalMeshSlot(const TArray<USkinnedMeshComponent*>& Comps)
	{
		if (Comps.IsEmpty()) return;
		USkeletalMesh* First = Comps[0]->GetSkeletalMesh();
		bool bMixed = false;
		for (int32 i = 1; i < Comps.Num(); ++i)
		{
			if (Comps[i]->GetSkeletalMesh() != First)
			{
				bMixed = true;
				break;
			}
		}

		const FString CurrentPath = (!bMixed && First) ? First->GetPath() : "";
		const FString Preview = bMixed ? "Multiple Values"
			: (First ? (CurrentPath.empty() ? First->GetName() : GetAssetDisplayName(CurrentPath)) : "None");
		ImGui::SetNextItemWidth(-1.0f);
		if (!ImGui::BeginCombo("##MultiSkeletalMesh", Preview.c_str()))
		{
			return;
		}
		auto ApplyAll = [&](USkeletalMesh* Mesh)
		{
			for (USkinnedMeshComponent* Comp : Comps)
			{
				Comp->SetSkeletalMesh(Mesh);
			}
		};
		if (ImGui::Selectable("None", !bMixed && First == nullptr)) ApplyAll(nullptr);
		for (TObjectIterator<USkeletalMesh> Itr; Itr; ++Itr)
		{
			const FString Path = Itr->GetPath();
			if (Path.empty()) continue;
			const bool bSelected = !bMixed && (*Itr == First);
			ImGui::PushID(*Itr);
			if (ImGui::Selectable(GetAssetDisplayName(Path).c_str(), bSelected)) ApplyAll(*Itr);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", Path.c_str());
			ImGui::PopID();
		}
		for (const FString& ItemPath : UAssetManager::GetUnloadedAssetPaths(EAssetType::SkeletalMesh))
		{
			ImGui::PushID(ItemPath.c_str());
			if (ImGui::Selectable(GetAssetDisplayName(ItemPath).c_str(), false))
			{
				if (USkeletalMesh* Mesh = UAssetManager::GetAssetByPath<USkeletalMesh>(ItemPath))
					ApplyAll(Mesh);
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ItemPath.c_str());
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	void DrawMultiMaterialSlots(const TArray<UMeshComponent*>& MeshComps, int32 NumSlots)
	{
		if (MeshComps.IsEmpty() || NumSlots <= 0) return;
		if (!ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) return;

		for (int32 Slot = 0; Slot < NumSlots; ++Slot)
		{
			ImGui::PushID(Slot);
			const FString SlotLabel = std::format("[{}] {}", Slot, MeshComps[0]->GetMaterialSlotName(Slot));
			const bool bOpen = ImGui::TreeNode(SlotLabel.c_str());

			bool bAnyOverride = false;
			for (UMeshComponent* MC : MeshComps)
			{
				if (MC->GetOverrideMaterial(Slot) != nullptr)
				{
					bAnyOverride = true;
					break;
				}
			}

			ImGui::SameLine();
			if (bAnyOverride)
			{
				ImGui::TextDisabled("Override");
				ImGui::SameLine();
				if (ImGui::SmallButton("Reset"))
				{
					for (UMeshComponent* MC : MeshComps)
					{
						MC->SetMaterial(Slot, nullptr);
					}
				}
			}
			else
			{
				ImGui::TextDisabled("Default");
			}

			if (bOpen)
			{
				if (BeginPropertyGridTable("MultiMaterialProperties"))
				{
					auto DrawTexRow = [&](int32 TexSlot, const char* RowLabel)
					{
						UMaterial* FirstEff = MeshComps[0]->GetMaterial(Slot);
						UTexture2D* FirstTex = (FirstEff && FirstEff->Textures.IsValidIndex(TexSlot)) ? FirstEff->Textures[TexSlot] : nullptr;
						bool bTexMixed = false;
						for (int32 i = 1; i < MeshComps.Num(); ++i)
						{
							UMaterial* Eff = MeshComps[i]->GetMaterial(Slot);
							UTexture2D* Tex = (Eff && Eff->Textures.IsValidIndex(TexSlot)) ? Eff->Textures[TexSlot] : nullptr;
							if (Tex != FirstTex) { bTexMixed = true; break; }
						}
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						DrawPropertyGridLabel(RowLabel);
						ImGui::TableSetColumnIndex(1);
						ImGui::PushID(TexSlot);
						DrawTexturePicker(bTexMixed ? nullptr : FirstTex, "##MultiTex",
							[&](UTexture2D* Picked)
							{
								for (UMeshComponent* MC : MeshComps)
								{
									UMaterial* Eff = MC->GetMaterial(Slot);
									UMaterial* Over = MC->GetOverrideMaterial(Slot);
									SetMaterialTexture(MC, Slot, TexSlot, Picked, Eff, Over);
								}
							});
						ImGui::PopID();
					};
					DrawTexRow(0, "Texture");
					DrawTexRow(1, "Normal Map");

					UMaterial* FirstEff = MeshComps[0]->GetMaterial(Slot);
					const FVector4 FirstColor = FirstEff ? FirstEff->BaseColor : FVector4(1, 1, 1, 1);
					float FirstShininess = FirstEff ? FirstEff->Shininess : 32.0f;
					FVector2 FirstUV = FirstEff ? FirstEff->UVScrollSpeed : FVector2();
					int FirstSampler = FirstEff ? static_cast<int>(FirstEff->SamplerState) : 0;
					int FirstBlend = FirstEff ? static_cast<int>(FirstEff->BlendState) : 0;

					bool bColorMixed = false, bShininessMixed = false, bUVMixed = false, bSamplerMixed = false, bBlendMixed = false;
					for (int32 i = 1; i < MeshComps.Num(); ++i)
					{
						UMaterial* E = MeshComps[i]->GetMaterial(Slot);
						const FVector4 C = E ? E->BaseColor : FVector4(1, 1, 1, 1);
						const float S = E ? E->Shininess : 32.0f;
						const FVector2 UV = E ? E->UVScrollSpeed : FVector2();
						const int Sm = E ? static_cast<int>(E->SamplerState) : 0;
						const int Bl = E ? static_cast<int>(E->BlendState) : 0;
						if (std::abs(C.X - FirstColor.X) > 1e-4f || std::abs(C.Y - FirstColor.Y) > 1e-4f ||
							std::abs(C.Z - FirstColor.Z) > 1e-4f || std::abs(C.W - FirstColor.W) > 1e-4f) bColorMixed = true;
						if (std::abs(S - FirstShininess) > 1e-4f) bShininessMixed = true;
						if (std::abs(UV.X - FirstUV.X) > 1e-4f || std::abs(UV.Y - FirstUV.Y) > 1e-4f) bUVMixed = true;
						if (Sm != FirstSampler) bSamplerMixed = true;
						if (Bl != FirstBlend) bBlendMixed = true;
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Base Color");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					FVector4 ColorVal = FirstColor;
					if (ImGui::ColorEdit4("##MultiBaseColor", &ColorVal.X))
					{
						for (UMeshComponent* MC : MeshComps)
						{
							if (UMaterial* Over = EnsureMaterialOverride(MC, Slot, MC->GetMaterial(Slot), MC->GetOverrideMaterial(Slot)))
								Over->BaseColor = ColorVal;
						}
					}
					if (bColorMixed) ImGui::TextDisabled("(Multiple Values)");

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Shininess");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					float ShininessVal = FirstShininess;
					if (ImGui::DragFloat("##MultiShininess", &ShininessVal, 0.5f, 1.0f, 256.0f, bShininessMixed ? "Multiple Values" : "%.3f"))
					{
						for (UMeshComponent* MC : MeshComps)
						{
							if (UMaterial* Over = EnsureMaterialOverride(MC, Slot, MC->GetMaterial(Slot), MC->GetOverrideMaterial(Slot)))
								Over->Shininess = ShininessVal;
						}
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("UV Scroll");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					FVector2 UVVal = FirstUV;
					if (ImGui::DragFloat2("##MultiUVScroll", &UVVal.X, 0.01f, 0.0f, 0.0f, bUVMixed ? "Multiple Values" : "%.3f"))
					{
						for (UMeshComponent* MC : MeshComps)
						{
							if (UMaterial* Over = EnsureMaterialOverride(MC, Slot, MC->GetMaterial(Slot), MC->GetOverrideMaterial(Slot)))
								Over->UVScrollSpeed = UVVal;
						}
					}

					const char* SamplerItems[] = { "Linear Clamp", "Linear Wrap" };
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Sampler");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::BeginCombo("##MultiSampler", bSamplerMixed ? "Multiple Values" : SamplerItems[FirstSampler]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(SamplerItems); ++i)
						{
							const bool bSel = !bSamplerMixed && FirstSampler == i;
							if (ImGui::Selectable(SamplerItems[i], bSel))
							{
								for (UMeshComponent* MC : MeshComps)
								{
									if (UMaterial* Over = EnsureMaterialOverride(MC, Slot, MC->GetMaterial(Slot), MC->GetOverrideMaterial(Slot)))
										Over->SamplerState = static_cast<ESamplerState>(i);
								}
							}
						}
						ImGui::EndCombo();
					}

					const char* BlendItems[] = { "Opaque", "Alpha Blend" };
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Blend Mode");
					ImGui::TableSetColumnIndex(1);
					ImGui::SetNextItemWidth(-1.0f);
					if (ImGui::BeginCombo("##MultiBlend", bBlendMixed ? "Multiple Values" : BlendItems[FirstBlend]))
					{
						for (int i = 0; i < IM_ARRAYSIZE(BlendItems); ++i)
						{
							const bool bSel = !bBlendMixed && FirstBlend == i;
							if (ImGui::Selectable(BlendItems[i], bSel))
							{
								for (UMeshComponent* MC : MeshComps)
								{
									if (UMaterial* Over = EnsureMaterialOverride(MC, Slot, MC->GetMaterial(Slot), MC->GetOverrideMaterial(Slot)))
										Over->BlendState = static_cast<EBlendState>(i);
								}
							}
						}
						ImGui::EndCombo();
					}

					EndPropertyGridTable();
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
			ImGui::Separator();
		}
	}
}

// UI 에디터처럼 다른 패널도 같은 속성 편집 UI를 쓰도록 위 함수들을 공개한다.
// 보조 함수들이 이 파일의 익명 네임스페이스에 있어 구현은 옮기지 않고 넘겨 준다.
namespace FPropertyEditor
{
	void RegisterStructDrawer(const UScriptStruct* Struct, FStructDrawer Drawer)
	{
		if (Struct && Drawer)
		{
			GetStructDrawers().Add(Struct, Drawer);
		}
	}

	bool DrawStructMember(UObject* Owner, void* StructPtr, const UScriptStruct& Struct, const char* MemberName,
		ImFont* CustomFont, const FPropertyMeta* MetaOverride)
	{
		const FProperty* Member = Struct.FindPropertyByName(MemberName);
		if (!Member)
		{
			ImGui::TextDisabled("(Missing %s)", MemberName);
			return false;
		}

		const bool bReadOnly = Member->HasAnyFlags(CPF_EditConst);
		if (bReadOnly) ImGui::BeginDisabled();
		const FPropertyMeta* EffectiveMeta = (MetaOverride && MetaOverride->IsSet()) ? MetaOverride : nullptr;
		const bool bChanged = DrawPropertyRow(Owner, StructPtr, *Member, CustomFont, EffectiveMeta);
		if (bReadOnly) ImGui::EndDisabled();
		return bChanged;
	}

	bool DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont)
	{
		return ::DrawProperty(Object, Property, CustomFont);
	}

	bool DrawObjectProperties(UObject* Object, ImFont* CustomFont)
	{
		return DrawProperties(Object, CustomFont);
	}

	bool DrawVector3(const char* Label, float* Values, float ResetValue)
	{
		return DrawVector3Controller(Label, Values, ResetValue, 55.0f);
	}

	bool DrawRotator(const char* Label, FRotator& Rotator)
	{
		return DrawRotatorAsXYZ(Label, Rotator);
	}
}

bool FDetailsPanel::Init()
{
	ImGuiIO& io = ImGui::GetIO();

	// 기본 폰트도 배율에 맞춰 다시 래스터화할 수 있도록 벡터 폰트를 명시한다.
	io.Fonts->AddFontDefaultVector();

	// 폰트 파일은 엔진 폴더 루트에 있다. 작업 디렉터리는 프로젝트 폴더이므로 절대 경로로 연다.
	const FString fontPath = FPaths::EngineFileUtf8("EngineResources/Fonts/NanumSquareR.ttf");
	float fontSize = 15.0f;
	const ImWchar* koreanRanges = io.Fonts->GetGlyphRangesKorean();

	CustomFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), fontSize, nullptr, koreanRanges);
	if (CustomFont) io.FontDefault = CustomFont;
	assert(CustomFont != nullptr);

	return true;
}

void FDetailsPanel::Tick(float DeltaTime)
{
}

const ULightComponent* FDetailsPanel::GetSelectedLight() const
{
	UActorComponent* Component = TargetComponent;
	while (Component && Component->IsHiddenInDetails())
	{
		USceneComponent* SceneComponent = Cast<USceneComponent>(Component);
		Component = SceneComponent ? SceneComponent->GetAttachParent() : nullptr;
	}
	return Cast<ULightComponent>(Component);
}

void FDetailsPanel::DrawShadowPreview()
{
	const ULightComponent* Light = GetSelectedLight();
	if (!Light || !QueryShadowPreview || !ImGuiRenderer) return;
	if (!ImGui::CollapsingHeader("Shadow Maps", ImGuiTreeNodeFlags_DefaultOpen)) return;
	FTexture2DArray* Atlas = nullptr;
	TArray<FLightShadowRenderData> Shadows;
	if (!QueryShadowPreview(Light, Atlas, Shadows) || !Atlas || !Atlas->GetWidth() || !Atlas->GetHeight())
	{
		ImGui::TextDisabled("No shadow map available for this light in the active view.");
		return;
	}
	ImGui::SliderFloat("Preview Contrast", &ShadowPreviewContrast, 1.0f, 256.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
	const float Width = float(Atlas->GetWidth());
	const float Height = float(Atlas->GetHeight());
	const float DisplayWidth = ImGui::GetContentRegionAvail().x;
	if (DisplayWidth <= 0) return;
	static const char* PointFaces[] = { "+X", "-X", "+Z", "-Z", "+Y", "-Y" };
	for (int32 i = 0; i < Shadows.Num(); ++i)
	{
		const auto& Shadow = Shadows[i];
		const FRect& Rect = Shadow.AtlasRect;
		if (Rect.Width <= 0 || Rect.Height <= 0) continue;
		ImGui::PushID(i);
		if (Light->GetLightType() == ELightType::Point)
			ImGui::Text("Face %s", PointFaces[i % 6]);
		else if (Light->GetLightType() == ELightType::Directional)
			ImGui::Text("Cascade %d", i + 1);
		ImGui::TextDisabled("Slice %u | %.0f x %.0f", Shadow.AtlasIndex, Rect.Width, Rect.Height);

		const ImVec2 UV0(Rect.X / Width, Rect.Y / Height);
		const ImVec2 UV1((Rect.X + Rect.Width) / Width, (Rect.Y + Rect.Height) / Height);

		if (!ImGuiRenderer->DrawShadowMap(Atlas->GetSRV(), Shadow.AtlasIndex,
			ImVec2(DisplayWidth, DisplayWidth * Rect.Height / Rect.Width), UV0, UV1, ShadowPreviewContrast))
			ImGui::TextDisabled("Shadow preview shader is unavailable.");
		ImGui::PopID();
	}
}


void FDetailsPanel::CommitPendingObjectEdit()
{
	if (!bPendingEditCommit)
	{
		return;
	}
	bPendingEditCommit = false;

	TArray<FPendingObjectEditSnapshot> Snapshots = std::move(PendingEditSnapshots);
	PendingEditSnapshots.Empty();

	if (!UndoHistory || !UndoHistory->CanRecord() || !World || Snapshots.IsEmpty())
	{
		return;
	}

	TArray<TUniquePtr<IEditorUndoCommand>> Commands;
	TArray<FName> EditedActorNames;
	for (FPendingObjectEditSnapshot& Snap : Snapshots)
	{
		if (Snap.BeforeBytes.IsEmpty())
		{
			continue;
		}
		AActor* Actor = EditorUndoUtils::FindActorByName(World, Snap.ActorName);
		if (!Actor)
		{
			continue;
		}
		UObject* TargetObj = Snap.ComponentName.IsNone()
			? static_cast<UObject*>(Actor)
			: static_cast<UObject*>(Actor->FindComponentByName(Snap.ComponentName));
		if (!TargetObj)
		{
			continue;
		}

		TArray<uint8> AfterBytes;
		if (EditorUndoUtils::CaptureObjectBytes(TargetObj, AfterBytes) && !(AfterBytes == Snap.BeforeBytes))
		{
			if (EditedActorNames.Find(Snap.ActorName) == INDEX_NONE)
			{
				EditedActorNames.Add(Snap.ActorName);
			}
			const char* Desc = Snap.ComponentName.IsNone() ? "Edit Actor Property" : "Edit Component Property";
			Commands.Add(EditorUndoUtils::MakeObjectBytesCommand(
				Desc,
				Snap.ActorName,
				Snap.ComponentName,
				std::move(Snap.BeforeBytes),
				std::move(AfterBytes)));
		}
	}

	if (Commands.Num() == 1)
	{
		UndoHistory->Push(std::move(Commands[0]));
	}
	else if (Commands.Num() > 1)
	{
		UndoHistory->Push(EditorUndoUtils::MakeCompositeCommand(
			"Edit Multiple Properties", std::move(Commands), EditedActorNames, EditedActorNames));
	}
}

void FDetailsPanel::OnRender()
{
	ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_FirstUseEver);

	ImGui::Begin("Details", GetOpenFlag());
	bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

	if (TargetActors.Num() > 1)
	{
		TArray<UObject*> ActorObjs;
		TArray<UObject*> RootCompObjs;
		TArray<FPendingObjectEditSnapshot> FrameBefore;
		for (AActor* Actor : TargetActors)
		{
			if (!Actor) continue;
			ActorObjs.Add(Actor);
			USceneComponent* Root = Actor->GetRootComponent();
			while (Root && Root->IsHiddenInDetails())
			{
				Root = Root->GetAttachParent();
			}
			if (Root)
			{
				RootCompObjs.Add(Root);
			}
		}

		// 다중 선택 대상 집합이 이전 대기 편집과 다르면 먼저 커밋한다.
		if (bPendingEditCommit)
		{
			bool bMatchesTracked = true;
			for (const FPendingObjectEditSnapshot& Snap : PendingEditSnapshots)
			{
				if (Snap.ComponentName.IsNone())
				{
					bool bFound = false;
					for (AActor* A : TargetActors)
					{
						if (A && A->GetFName() == Snap.ActorName) { bFound = true; break; }
					}
					if (!bFound) { bMatchesTracked = false; break; }
				}
			}
			if (!bMatchesTracked) CommitPendingObjectEdit();
		}

		const bool bAllInWorld = [&]()
		{
			for (AActor* A : TargetActors)
			{
				if (!A || A->GetWorld() != World) return false;
			}
			return true;
		}();
		const bool bCanCaptureBefore = UndoHistory && UndoHistory->CanRecord() && bAllInWorld &&
			!bPendingEditCommit &&
			(bFocused || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) ||
			 ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));

		if (bCanCaptureBefore)
		{
			for (AActor* A : TargetActors)
			{
				if (!A) continue;
				FPendingObjectEditSnapshot ActorSnap;
				ActorSnap.ActorName = A->GetFName();
				if (EditorUndoUtils::CaptureObjectBytes(A, ActorSnap.BeforeBytes))
				{
					FrameBefore.Add(std::move(ActorSnap));
				}
				USceneComponent* Root = A->GetRootComponent();
				while (Root && Root->IsHiddenInDetails()) Root = Root->GetAttachParent();
				if (Root)
				{
					FPendingObjectEditSnapshot CompSnap;
					CompSnap.ActorName = A->GetFName();
					CompSnap.ComponentName = Root->GetFName();
					if (EditorUndoUtils::CaptureObjectBytes(Root, CompSnap.BeforeBytes))
					{
						FrameBefore.Add(std::move(CompSnap));
					}
				}
			}
		}

		const TArray<UClass*> CommonActorClasses = GetCommonClassChain(ActorObjs);
		const char* CommonActorTypeName = !CommonActorClasses.IsEmpty() ? CommonActorClasses.Last()->Name.c_str() : "AActor";
		ImGui::Text("%d Actors Selected (%s)", TargetActors.Num(), CommonActorTypeName);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			const int32 ShowLimit = (std::min)(TargetActors.Num(), 12);
			for (int32 i = 0; i < ShowLimit; ++i)
			{
				if (TargetActors[i]) ImGui::TextUnformatted(TargetActors[i]->GetName().c_str());
			}
			if (TargetActors.Num() > ShowLimit)
			{
				ImGui::TextDisabled("... +%d more", TargetActors.Num() - ShowLimit);
			}
			ImGui::EndTooltip();
		}
		ImGui::Separator();

		ImGui::PushID("MultiActorProperties");
		DrawMultiProperties(ActorObjs, CustomFont);
		ImGui::PopID();

		if (RootCompObjs.Num() == ActorObjs.Num() && !RootCompObjs.IsEmpty())
		{
			ImGui::PushID("MultiComponentProperties");
			ImGui::SeparatorText("Shared Component Properties");
			DrawMultiProperties(RootCompObjs, CustomFont);

			TArray<USkinnedMeshComponent*> SkinnedComps;
			TArray<UMeshComponent*> MeshComps;
			int32 MinMaterialSlots = INT_MAX;
			for (UObject* Obj : RootCompObjs)
			{
				if (USkinnedMeshComponent* Sk = Cast<USkinnedMeshComponent>(Obj))
				{
					SkinnedComps.Add(Sk);
				}
				if (UMeshComponent* MC = Cast<UMeshComponent>(Obj))
				{
					MeshComps.Add(MC);
					MinMaterialSlots = (std::min)(MinMaterialSlots, MC->GetNumMaterials());
				}
			}
			if (SkinnedComps.Num() == RootCompObjs.Num() && BeginPropertyGridTable("MultiMeshProperties"))
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				DrawPropertyGridLabel("Skeletal Mesh");
				ImGui::TableSetColumnIndex(1);
				DrawMultiSkeletalMeshSlot(SkinnedComps);
				EndPropertyGridTable();
			}
			if (MeshComps.Num() == RootCompObjs.Num() && MinMaterialSlots > 0 && MinMaterialSlots != INT_MAX)
			{
				DrawMultiMaterialSlots(MeshComps, MinMaterialSlots);
			}
			ImGui::PopID();
		}

		if (bCanCaptureBefore && !FrameBefore.IsEmpty())
		{
			bool bAnyChanged = false;
			for (const FPendingObjectEditSnapshot& Snap : FrameBefore)
			{
				AActor* A = EditorUndoUtils::FindActorByName(World, Snap.ActorName);
				UObject* TargetObj = A ? (Snap.ComponentName.IsNone()
					? static_cast<UObject*>(A)
					: static_cast<UObject*>(A->FindComponentByName(Snap.ComponentName))) : nullptr;
				TArray<uint8> AfterBytes;
				if (TargetObj && EditorUndoUtils::CaptureObjectBytes(TargetObj, AfterBytes) && !(AfterBytes == Snap.BeforeBytes))
				{
					bAnyChanged = true;
					break;
				}
			}
			if (bAnyChanged)
			{
				PendingEditSnapshots = std::move(FrameBefore);
				bPendingEditCommit = true;
			}
		}

		if (bPendingEditCommit && !ImGui::IsAnyItemActive())
		{
			CommitPendingObjectEdit();
		}
	}
	else if (TargetActor)
	{
		// 빌보드처럼 디테일에 숨겨진 컴포넌트가 선택되면 노출되는 부모로 올린다.
		// (예: 라이트는 빌보드를 클릭해서 고르지만 수치는 SpotLight 쪽에 있다)
		while (TargetComponent && TargetComponent->IsHiddenInDetails())
		{
			USceneComponent* HiddenComponent = Cast<USceneComponent>(TargetComponent);
			TargetComponent = HiddenComponent ? HiddenComponent->GetAttachParent() : nullptr;
		}

		UActorComponent* SelectCandidateComponent = TargetComponent;

		DrawActorHeader(TargetActor, TargetComponent, UndoHistory);
		DrawCompoenetList(TargetActor, TargetComponent);
		ImGui::Separator();

		const FName CurrentActorName = TargetActor->GetFName();
		const FName CurrentCompName = TargetComponent ? TargetComponent->GetFName() : FName();
		if (bPendingEditCommit)
		{
			const bool bMatchesSingle = PendingEditSnapshots.Num() <= 2 && !PendingEditSnapshots.IsEmpty()
				&& PendingEditSnapshots[0].ActorName == CurrentActorName
				&& (PendingEditSnapshots.Num() == 1
					? (PendingEditSnapshots[0].ComponentName == FName() || PendingEditSnapshots[0].ComponentName == CurrentCompName)
					: (PendingEditSnapshots[1].ComponentName == CurrentCompName));
			if (!bMatchesSingle)
			{
				CommitPendingObjectEdit();
			}
		}

		const bool bCanCaptureBefore = UndoHistory && UndoHistory->CanRecord() && TargetActor->GetWorld() == World &&
			!bPendingEditCommit &&
			(bFocused || ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) ||
			 ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId));

		TArray<uint8> FrameBeforeActorBytes;
		TArray<uint8> FrameBeforeCompBytes;
		if (bCanCaptureBefore)
		{
			EditorUndoUtils::CaptureObjectBytes(TargetActor, FrameBeforeActorBytes);
			if (TargetComponent)
			{
				EditorUndoUtils::CaptureObjectBytes(TargetComponent, FrameBeforeCompBytes);
			}
		}

		// 액터 -> 컴포넌트 순으로, 클래스별 프로퍼티 표시
		ImGui::PushID(TargetActor);
		DrawProperties(TargetActor, CustomFont);
		ImGui::PopID();

		if (TargetComponent)
		{
			ImGui::PushID(TargetComponent);
			ImGui::SeparatorText("Component Properties");
			DrawProperties(TargetComponent, CustomFont);
			if (auto* Component = Cast<UScriptComponent>(TargetComponent))
			{
				DrawScriptSlot(Component, CustomFont);
			}
			if (auto* Component = Cast<USkinnedMeshComponent>(TargetComponent))
			{
				if (BeginPropertyGridTable("SkinnedMeshProperties"))
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					DrawPropertyGridLabel("Skeletal Mesh");
					ImGui::TableSetColumnIndex(1);
					DrawSkeletalMeshSlot(Component);

					if (auto* SkelComp = Cast<USkeletalMeshComponent>(TargetComponent))
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						DrawPropertyGridLabel("Animation Blueprint");
						ImGui::TableSetColumnIndex(1);
						DrawAnimBlueprintSlot(SkelComp);

						const bool bUseBlueprint = !SkelComp->GetAnimBlueprintPath().empty();
						ImGui::BeginDisabled(bUseBlueprint);
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						DrawPropertyGridLabel("Single Anim Asset");
						ImGui::TableSetColumnIndex(1);
						DrawAnimationSlot(SkelComp);
						ImGui::EndDisabled();

						if (bUseBlueprint)
						{
							ImGui::TableNextRow();
							ImGui::TableSetColumnIndex(0);
							ImGui::TableSetColumnIndex(1);
							ImGui::TextWrapped("Choose None in Animation Blueprint to use a single animation.");
						}
					}
					EndPropertyGridTable();
				}
			}
			DrawShadowPreview();

			if (SelectCandidateComponent != TargetComponent)
			{
				SelectComponent(TargetComponent);
			}
			if (UMeshComponent* MeshComponent = Cast<UMeshComponent>(TargetComponent))
			{
				DrawMaterialSlots(MeshComponent);
			}
			ImGui::PopID();
		}

		if (bCanCaptureBefore)
		{
			TArray<uint8> FrameAfterActorBytes;
			TArray<uint8> FrameAfterCompBytes;
			EditorUndoUtils::CaptureObjectBytes(TargetActor, FrameAfterActorBytes);
			if (TargetComponent)
			{
				EditorUndoUtils::CaptureObjectBytes(TargetComponent, FrameAfterCompBytes);
			}

			if (!(FrameAfterActorBytes == FrameBeforeActorBytes) || !(FrameAfterCompBytes == FrameBeforeCompBytes))
			{
				PendingEditSnapshots.Empty();
				if (!(FrameAfterActorBytes == FrameBeforeActorBytes))
				{
					PendingEditSnapshots.Add({ CurrentActorName, FName(), std::move(FrameBeforeActorBytes) });
				}
				if (TargetComponent && !(FrameAfterCompBytes == FrameBeforeCompBytes))
				{
					PendingEditSnapshots.Add({ CurrentActorName, CurrentCompName, std::move(FrameBeforeCompBytes) });
				}
				bPendingEditCommit = true;
			}
		}

		if (bPendingEditCommit && !ImGui::IsAnyItemActive())
		{
			CommitPendingObjectEdit();
		}

		if (PendingDeleteComponent)
		{
			if (DeleteCallback)
			{
				DeleteCallback(PendingDeleteComponent);
			}

			PendingDeleteComponent = nullptr;
		}
	}
	else if (bPendingEditCommit)
	{
		CommitPendingObjectEdit();
	}
	ImGui::End();
}


void FDetailsPanel::SelectComponent(UActorComponent* Component)
{
	if (!Component)
	{
		TargetComponent = nullptr;
		EditingNameComponent = nullptr;
		bFocusNameEdit = false;

		if (Callback)
			Callback(nullptr);

		return;
	}

	TargetComponent = Component;

	if (Callback)
		Callback(TargetComponent);
}

void FDetailsPanel::DrawCompoenetList(AActor* SelectedActor, UActorComponent*& OutSelectedComponent)
{
	if (!SelectedActor) { return; }

	if (PendingDragComponent && PendingDroppedComponent)
	{
		TryReparent(PendingDroppedComponent, PendingDragComponent);
		PendingDragComponent = nullptr;
		PendingDroppedComponent = nullptr;
	}

    if (PendingRootComponent)
    {
        USceneComponent* NewRoot = PendingRootComponent;
        PendingRootComponent = nullptr;
        USceneComponent* OldRoot = SelectedActor->GetRootComponent();
        USceneComponent* ExtParent = (OldRoot && OldRoot->GetAttachParent() && OldRoot->GetAttachParent()->GetOwner() != SelectedActor)
            ? OldRoot->GetAttachParent() : nullptr;
        const FName OldRootName = OldRoot ? OldRoot->GetFName() : FName();
        const FName NewRootName = NewRoot ? NewRoot->GetFName() : FName();
        const FName ExtParentActor = (ExtParent && ExtParent->GetOwner()) ? ExtParent->GetOwner()->GetFName() : FName();
        const FName ExtParentComp = ExtParent ? ExtParent->GetFName() : FName();
        if (SelectedActor->MakeRootComponent(NewRoot))
        {
            if (UndoHistory && UndoHistory->CanRecord())
            {
                UndoHistory->Push(EditorUndoUtils::MakeRootComponentCommand(
                    SelectedActor->GetFName(), OldRootName, NewRootName, ExtParentActor, ExtParentComp));
            }
            OutSelectedComponent = NewRoot;
            if (Callback) Callback(NewRoot);
        }
    }
    USceneComponent* Root = SelectedActor->GetRootComponent();

    // UE처럼 컴포넌트 트리를 액터 이름 아래 별도 테두리 상자에 둔다. 아래 가장자리를 끌어 높이를 바꿀 수 있다.
    int32 RowCount = 1;
    for (UActorComponent* Component : SelectedActor->GetComponents())
        if (Component && !Component->IsHiddenInDetails()) ++RowCount;
    const float RowHeight = ImGui::GetFrameHeight();
    const float BoxHeight = std::clamp(static_cast<float>(RowCount), 3.0f, 10.0f) * RowHeight + ImGui::GetStyle().WindowPadding.y * 2.0f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, EditorColor::BgDeepest);
    const bool bBoxVisible = ImGui::BeginChild("##ComponentTree", { 0.0f, BoxHeight },
        ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    ImGui::PopStyleColor();
    if (!bBoxVisible) { ImGui::EndChild(); return; }

    // 첫 줄: "액터 이름 (Instance)". 누르면 루트 컴포넌트를 고른다. 여기에 끌어 놓으면 루트에 붙는다.
    {
        const FString InstanceLabel = SelectedActor->GetName() + " (Instance)";
        // 선택 표시는 아래 루트 컴포넌트 줄이 맡으므로 이 줄은 강조하지 않는다.
        if (ImGui::Selectable(InstanceLabel.c_str(), false) && Root)
            OutSelectedComponent = Root;
        if (ImGui::BeginDragDropTarget())
        {
            USceneComponent* Child = nullptr;
            if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
                Child = *static_cast<USceneComponent* const*>(Payload->Data);
            if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
                Child = (*static_cast<AActor* const*>(Payload->Data))->GetRootComponent();
            if (EditorHierarchy::CanAttach(Child, Root)) { PendingDroppedComponent = Child; PendingDragComponent = Root; }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::Indent(EditorStyle::Scale(12.0f));
    struct FTreeScope
    {
        ~FTreeScope() { ImGui::Unindent(EditorStyle::Scale(12.0f)); ImGui::EndChild(); }
    } TreeScope;
    if (Root) DrawSceneComponentNode(Root, Root, OutSelectedComponent);
    bool bNeedsSeparator = Root && !Root->IsHiddenInDetails();

	// Draw ActorComponent
	for (UActorComponent* ActorComp : SelectedActor->GetComponents())
	{
		if (!ActorComp || ActorComp->IsHiddenInDetails() || Cast<USceneComponent>(ActorComp))
		{
			continue;
		}

        if (bNeedsSeparator) { ImGui::Separator(); bNeedsSeparator = false; }
		ImGui::PushID(ActorComp);
		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (ActorComp == OutSelectedComponent)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		FString Label = ActorComp->GetName();
		const ImVec2 RowStart = ImGui::GetCursorScreenPos();
		const float RowWidth = ImGui::GetContentRegionAvail().x;
		const float MaxLabelWidth = RowWidth - ImGui::GetTreeNodeToLabelSpacing() - ImGui::GetStyle().FramePadding.x;
		const FString DisplayedLabel = TruncateTextToWidth(Label, MaxLabelWidth);
		const bool bEditingThis = (EditingNameComponent == ActorComp);

		ImGui::TreeNodeEx("ActorComponentNode", Flags, "%s", bEditingThis ? "" : DisplayedLabel.c_str());
		const bool bRowHovered = ImGui::IsItemHovered();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			OutSelectedComponent = ActorComp;
		}

        bool Rename = bRowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        if (ImGui::BeginPopupContextItem("Component Context"))
        {
            if (ImGui::MenuItem("Rename", "F2")) Rename = true;
            if (ImGui::MenuItem("Delete", nullptr, false, ActorComp != Root))
				PendingDeleteComponent = ActorComp;
			ImGui::EndPopup();
		}

		DrawComponentNameEditor(ActorComp, OutSelectedComponent, Rename);
		if (EditingNameComponent != ActorComp)
		{
			const char* ClassRawName = ActorComp->GetClass() ? ActorComp->GetClass()->Name.c_str() : "";
			DrawComponentClassSuffix(DisplayedLabel, Label, ClassRawName, RowStart, RowWidth, bRowHovered);
		}

		ImGui::PopID();
	}
}

void FDetailsPanel::DrawSceneComponentNode(USceneComponent* Component, USceneComponent* Root, UActorComponent*& OutSelectedComponent)
{
	// 빌보드 같은 에디터 표시용 컴포넌트는 트리에 노출하지 않는다
	if (!Component || Component->IsHiddenInDetails())
		return;

	// 숨겨진 자식만 있는 노드가 펼침 화살표를 갖지 않도록 노출되는 자식만 센다
	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
	{
		if (Child && Child->GetOwner() == Component->GetOwner() && !Child->IsHiddenInDetails())
		{
			Children.Add(Child);
		}
	}

	const bool bIsLeaf = Children.IsEmpty();
	bIsEditingName = (EditingNameComponent == Component);

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (Component == OutSelectedComponent)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (bIsLeaf)
	{
		Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
	}

	FString Label = Component->GetName();
	if (Component == Root)
		Label += " (Root)";

	ImGui::PushID(Component);
	const ImVec2 RowStart = ImGui::GetCursorScreenPos();
	const float RowWidth = ImGui::GetContentRegionAvail().x;
	const float MaxLabelWidth = RowWidth - ImGui::GetTreeNodeToLabelSpacing() - ImGui::GetStyle().FramePadding.x;
	const FString DisplayedLabel = TruncateTextToWidth(Label, MaxLabelWidth);
	const bool bOpen = ImGui::TreeNodeEx("ComponentNode", Flags, "%s", bIsEditingName ? "" : DisplayedLabel.c_str());
	const bool bRowHovered = ImGui::IsItemHovered();

	USceneComponent* DragComponent = Component;

	// source: Root 제외, 현재 Component를 payload에 담음
	if (EditingNameComponent != Component && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(EditorDragDrop::Component, &Component, sizeof(USceneComponent*));
        ImGui::TextUnformatted(Component->GetName().c_str());
		ImGui::EndDragDropSource();
	}
	// target: 모든 행이 가능, payload를 받아 현재 Component를 부모로 사용
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Component))
		{
			USceneComponent* DroppedComponent = *static_cast<USceneComponent**>(Payload->Data);
            if (EditorHierarchy::CanAttach(DroppedComponent, DragComponent))
            { PendingDroppedComponent = DroppedComponent; PendingDragComponent = DragComponent; }
		}
        if (const auto* Payload = ImGui::AcceptDragDropPayload(EditorDragDrop::Actor))
        {
            auto* Dropped = (*static_cast<AActor* const*>(Payload->Data))->GetRootComponent();
            if (EditorHierarchy::CanAttach(Dropped, Component))
            { PendingDroppedComponent = Dropped; PendingDragComponent = Component; }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) OutSelectedComponent = Component;
    bool Rename = (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left));
    if (ImGui::BeginPopupContextItem("Component Context"))
    {
        if (ImGui::MenuItem("Rename", "F2")) Rename = true;
        if (ImGui::MenuItem("Make Root Component", nullptr, false, Component != Root)) PendingRootComponent = Component;
        if (ImGui::MenuItem("Delete", nullptr, false, Component != Root)) PendingDeleteComponent = Component;
        ImGui::EndPopup();
    }
    DrawComponentNameEditor(Component, OutSelectedComponent, Rename);
    if (EditingNameComponent != Component)
    {
        const char* ClassRawName = Component->GetClass() ? Component->GetClass()->Name.c_str() : "";
        DrawComponentClassSuffix(DisplayedLabel, Label, ClassRawName, RowStart, RowWidth, bRowHovered);
    }

	if (bOpen && !bIsLeaf)
	{
		for (USceneComponent* Child : Children)
		{
			DrawSceneComponentNode(Child, Root, OutSelectedComponent);
		}
		ImGui::TreePop();
	}

	ImGui::PopID();
}

void FDetailsPanel::DrawComponentNameEditor(UActorComponent* Component, UActorComponent*& Selected, bool bRequested)
{
    const bool bF2 = Component == Selected && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F2, false);
    if (bRequested || bF2)
    {
        Selected = Component;
        EditingNameComponent = Component;
        strncpy_s(ComponentNameEditBuffer, Component->GetName().c_str(), _TRUNCATE);
        bFocusNameEdit = true;
    }
    if (EditingNameComponent != Component) return;
    if (bFocusNameEdit) { ImGui::SetKeyboardFocusHere(); bFocusNameEdit = false; }
    const bool bEnter = ImGui::InputText("##ComponentRename", ComponentNameEditBuffer, sizeof(ComponentNameEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue);
    const bool bCancel = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (bEnter || bCancel || ImGui::IsItemDeactivated())
    {
        if (!bCancel && ComponentNameEditBuffer[0])
        {
            FName Name(ComponentNameEditBuffer);
            UActorComponent* Existing = Component->GetOwner()->FindComponentByName(Name);
            if (!Existing || Existing == Component)
            {
                const FName OldName = Component->GetFName();
                if (OldName != Name)
                {
                    Component->SetName(Name);
                    if (UndoHistory && UndoHistory->CanRecord() && Component->GetOwner())
                    {
                        UndoHistory->Push(EditorUndoUtils::MakeComponentRenameCommand(
                            Component->GetOwner()->GetFName(), OldName, Name));
                    }
                }
            }
            else KRS_LOG(Warning, "A component with this name already exists in this actor.");
        }
        EditingNameComponent = nullptr;
    }
}

void FDetailsPanel::TryReparent(USceneComponent* DroppedComponent, USceneComponent* DragComponent)
{
    if (!DroppedComponent)
    {
        return;
    }
    AActor* ChildActor = DroppedComponent->GetOwner();
    USceneComponent* OldParent = DroppedComponent->GetAttachParent();
    const FName ChildActorName = ChildActor ? ChildActor->GetFName() : FName();
    const FName ChildCompName = DroppedComponent->GetFName();
    const FName OldParentActorName = (OldParent && OldParent->GetOwner()) ? OldParent->GetOwner()->GetFName() : FName();
    const FName OldParentCompName = OldParent ? OldParent->GetFName() : FName();

    if (EditorHierarchy::Attach(DroppedComponent, DragComponent) &&
        UndoHistory && UndoHistory->CanRecord() && !ChildActorName.IsNone())
    {
        USceneComponent* NewParent = DroppedComponent->GetAttachParent();
        const FName NewParentActorName = (NewParent && NewParent->GetOwner()) ? NewParent->GetOwner()->GetFName() : FName();
        const FName NewParentCompName = NewParent ? NewParent->GetFName() : FName();
        UndoHistory->Push(EditorUndoUtils::MakeHierarchyAttachCommand(
            "Reparent Component",
            ChildActorName, ChildCompName,
            OldParentActorName, OldParentCompName,
            NewParentActorName, NewParentCompName));
    }
}


void FDetailsPanel::OnActorDestroyed(AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	if (EditingNameComponent && EditingNameComponent->GetOwner() == Actor)
	{
		EditingNameComponent = nullptr;
		bIsEditingName = false;
	}
	if (PendingRootComponent && PendingRootComponent->GetOwner() == Actor)
	{
		PendingRootComponent = nullptr;
	}
	if (PendingDeleteComponent && PendingDeleteComponent->GetOwner() == Actor)
	{
		PendingDeleteComponent = nullptr;
	}
	if (PendingDroppedComponent && PendingDroppedComponent->GetOwner() == Actor)
	{
		PendingDroppedComponent = nullptr;
	}
	if (PendingDragComponent && PendingDragComponent->GetOwner() == Actor)
	{
		PendingDragComponent = nullptr;
	}

	const int32 Idx = TargetActors.Find(Actor);
	if (Idx != INDEX_NONE)
	{
		TargetActors.RemoveAt(Idx);
	}

	if (TargetActor == Actor)
	{
		bPendingEditCommit = false;
		PendingEditSnapshots.Empty();
		TargetActor = TargetActors.IsEmpty() ? nullptr : TargetActors.Last();
		TargetComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	}
	else if (TargetComponent && TargetComponent->GetOwner() == Actor)
	{
		TargetComponent = TargetActor ? TargetActor->GetRootComponent() : nullptr;
	}
}

FDetailsPanel::~FDetailsPanel()
{
	//delete transform;
}
