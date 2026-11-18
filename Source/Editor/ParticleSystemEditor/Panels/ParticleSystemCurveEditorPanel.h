#pragma once

#include "ParticleSystemEditorPanel.h"

struct FFloatCurveKey;

class FParticleSystemCurveEditorPanel : public FParticleSystemEditorPanel
{
public:
	explicit FParticleSystemCurveEditorPanel(FParticleSystemEditor& InEditor);

protected:
	void DrawContent() override;
	ImGuiWindowFlags GetWindowFlags() const override
	{
		return ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
	}

private:
	struct FCurveTrack
	{
		UParticleModule* Module = nullptr;
		const FStructProperty* Property = nullptr;
		TArray<FFloatCurveKey>* Keys = nullptr;
		FString Id;
		FString Label;
		ImU32 Color = 0;
	};

	struct FCurveView
	{
		float TimeMin = 0.0f;
		float TimeMax = 1.0f;
		float ValueMin = -0.1f;
		float ValueMax = 1.1f;
		ImVec2 Origin{};
		ImVec2 Size{};

		float PixelsPerTime() const { return Size.x / (TimeMax - TimeMin); }
		float PixelsPerValue() const { return Size.y / (ValueMax - ValueMin); }
		float TimeToX(float T) const { return Origin.x + (T - TimeMin) * PixelsPerTime(); }
		float ValueToY(float V) const { return Origin.y + Size.y - (V - ValueMin) * PixelsPerValue(); }
		float XToTime(float X) const { return TimeMin + (X - Origin.x) / PixelsPerTime(); }
		float YToValue(float Y) const { return ValueMin + (Origin.y + Size.y - Y) / PixelsPerValue(); }
		ImVec2 ToScreen(float T, float V) const { return { TimeToX(T), ValueToY(V) }; }
	};

	enum class EDragMode : uint8 { None, Keys, ArriveTangent, LeaveTangent };

	struct FKeyRef
	{
		FString TrackId;
		int32 KeyId = -1;
	};

	struct FDragOrigin
	{
		FString TrackId;
		int32 KeyId;
		float Time;
		float Value;
	};

	struct FHit
	{
		EDragMode Mode = EDragMode::None;
		int32 TrackIndex = -1;
		int32 KeyId = -1;
	};

	void CollectTracks();
	void CollectModuleTracks(UParticleModule* Module);
	FCurveTrack* FindTrack(const FString& Id);
	bool IsVisible(const FCurveTrack& Track) const { return !HiddenTracks.contains(Track.Id); }

	void DrawToolbar();
	void DrawTrackList();
	void DrawCanvas();
	void DrawGrid(ImDrawList* DrawList) const;
	void DrawCurve(ImDrawList* DrawList, const FCurveTrack& Track) const;
	void DrawKeys(ImDrawList* DrawList) const;
	void DrawContextMenu();

	void HandleViewInput(bool bHovered);
	void HandleKeyInput(bool bHovered);
	FHit HitTest(const ImVec2& Mouse) const;
	ImVec2 TangentHandlePos(const FFloatCurveKey& Key, float Tangent, float Dir) const;
	void BeginKeyDrag(const ImVec2& Mouse);
	void AddKeyAt(const ImVec2& Mouse);
	void DeleteSelectedKeys();
	void FitView();

	bool IsSelected(const FString& TrackId, int32 KeyId) const;
	FFloatCurveKey* ResolveKey(const FKeyRef& Ref, FCurveTrack** OutTrack = nullptr);

	void Commit(const FCurveTrack& Track) const;

	TArray<FCurveTrack> Tracks;
	std::unordered_set<FString> HiddenTracks;
	FString ActiveTrackId;

	FCurveView View;
	TArray<FKeyRef> SelectedKeys;
	EDragMode DragMode = EDragMode::None;
	FKeyRef DragKey;
	ImVec2 DragStartMouse{};
	ImVec2 DragGrabOffset{}; // 탄젠트 핸들 중심 - 클릭 지점
	TArray<FDragOrigin> DragOrigins;
	bool bFitPending = true;
};
