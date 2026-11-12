#pragma once

#include "Math/EngineMath.h"
#include "Math/Rect.h"
#include "Math/Vector2.h"

#include <cassert>

// UE의 뷰포트 레이아웃(1~4 패널)을 따른다. 저장 파일과 호환되도록 앞의 두 값은 순서를 유지한다.
// 이름의 방향(Left, Right, Top, Bottom)은 큰 패널이 놓이는 쪽이다. 큰 패널은 항상 View 0이 맡는다.
enum class ELayoutMode
{
	Single,
	QuadSplit,          // 2x2
	TwoPanes,           // 좌우
	TwoPanesStacked,    // 위아래
	ThreePanesLeft,
	ThreePanesRight,
	ThreePanesTop,
	ThreePanesBottom,
	FourPanesLeft,
	FourPanesRight,
	FourPanesTop,
	FourPanesBottom,
	Count
};
enum class EDragAxis { Horizontal, Vertical };

// 설정 파일에 쓰는 이름. "Single", "Quad"는 이전 버전 파일과 같다.
inline const char* GetLayoutModeName(ELayoutMode Mode)
{
	static const char* Names[] = { "Single", "Quad", "TwoPanes", "TwoPanesStacked",
		"ThreePanesLeft", "ThreePanesRight", "ThreePanesTop", "ThreePanesBottom",
		"FourPanesLeft", "FourPanesRight", "FourPanesTop", "FourPanesBottom" };
	static_assert(sizeof(Names) / sizeof(Names[0]) == static_cast<size_t>(ELayoutMode::Count));
	const int Index = static_cast<int>(Mode);
	return Index >= 0 && Index < static_cast<int>(ELayoutMode::Count) ? Names[Index] : "Quad";
}

inline bool ParseLayoutModeName(const char* Name, ELayoutMode& OutMode)
{
	for (int Index = 0; Index < static_cast<int>(ELayoutMode::Count); ++Index)
	{
		const char* Candidate = GetLayoutModeName(static_cast<ELayoutMode>(Index));
		const char* A = Name;
		const char* B = Candidate;
		while (*A && *A == *B) { ++A; ++B; }
		if (*A == '\0' && *B == '\0')
		{
			OutMode = static_cast<ELayoutMode>(Index);
			return true;
		}
	}
	return false;
}

inline int32 GetLayoutPaneCount(ELayoutMode Mode)
{
	switch (Mode)
	{
	case ELayoutMode::Single: return 1;
	case ELayoutMode::TwoPanes:
	case ELayoutMode::TwoPanesStacked: return 2;
	case ELayoutMode::ThreePanesLeft:
	case ELayoutMode::ThreePanesRight:
	case ELayoutMode::ThreePanesTop:
	case ELayoutMode::ThreePanesBottom: return 3;
	default: return 4;
	}
}

struct FSplitRatio
{
	float Horizontal = 0.5f;
	float Vertical = 0.5f;
};

inline FSplitRatio ClampSplitRatio(const FSplitRatio& Ratio, float MinRatio)
{
	assert(MinRatio >= 0.0f && MinRatio <= 0.5f);
	return {
		FMath::Clamp(Ratio.Horizontal, MinRatio, 1.0f - MinRatio),
		FMath::Clamp(Ratio.Vertical, MinRatio, 1.0f - MinRatio)
	};
}

inline FSplitRatio ApplySplitterDrag(const FSplitRatio& Current, EDragAxis Axis,
	float DeltaPixels, FVector2 WindowSize, float MinRatio)
{
	FSplitRatio Result = Current;
	if (Axis == EDragAxis::Horizontal)
	{
		assert(WindowSize.X > 0.0f);
		Result.Horizontal += DeltaPixels / WindowSize.X;
	}
	else
	{
		assert(WindowSize.Y > 0.0f);
		Result.Vertical += DeltaPixels / WindowSize.Y;
	}
	return ClampSplitRatio(Result, MinRatio);
}

inline void ComputeViewRects(const FSplitRatio& Ratio, FVector2 WindowSize, FRect OutRects[4])
{
	const float SplitX = WindowSize.X * Ratio.Horizontal;
	const float SplitY = WindowSize.Y * Ratio.Vertical;
	OutRects[0] = { 0.0f, 0.0f, SplitX, SplitY };
	OutRects[1] = { SplitX, 0.0f, WindowSize.X - SplitX, SplitY };
	OutRects[2] = { 0.0f, SplitY, SplitX, WindowSize.Y - SplitY };
	OutRects[3] = { SplitX, SplitY, WindowSize.X - SplitX, WindowSize.Y - SplitY };
}

// Single을 제외한 레이아웃의 패널 사각형. 쓰지 않는 슬롯은 빈 사각형이다.
// Horizontal 비율은 세로 분할선(SplitX), Vertical 비율은 가로 분할선(SplitY)의 위치다.
inline void ComputeLayoutRects(ELayoutMode Mode, const FSplitRatio& Ratio, FVector2 Size, FRect OutRects[4])
{
	const float W = Size.X, H = Size.Y;
	const float SX = W * Ratio.Horizontal, SY = H * Ratio.Vertical;
	for (int32 i = 0; i < 4; ++i) OutRects[i] = {};
	switch (Mode)
	{
	case ELayoutMode::Single:
		OutRects[0] = { 0.0f, 0.0f, W, H };
		break;
	case ELayoutMode::QuadSplit:
		ComputeViewRects(Ratio, Size, OutRects);
		break;
	case ELayoutMode::TwoPanes:
		OutRects[0] = { 0.0f, 0.0f, SX, H };
		OutRects[1] = { SX, 0.0f, W - SX, H };
		break;
	case ELayoutMode::TwoPanesStacked:
		OutRects[0] = { 0.0f, 0.0f, W, SY };
		OutRects[1] = { 0.0f, SY, W, H - SY };
		break;
	case ELayoutMode::ThreePanesLeft:
		OutRects[0] = { 0.0f, 0.0f, SX, H };
		OutRects[1] = { SX, 0.0f, W - SX, SY };
		OutRects[2] = { SX, SY, W - SX, H - SY };
		break;
	case ELayoutMode::ThreePanesRight:
		OutRects[0] = { SX, 0.0f, W - SX, H };
		OutRects[1] = { 0.0f, 0.0f, SX, SY };
		OutRects[2] = { 0.0f, SY, SX, H - SY };
		break;
	case ELayoutMode::ThreePanesTop:
		OutRects[0] = { 0.0f, 0.0f, W, SY };
		OutRects[1] = { 0.0f, SY, SX, H - SY };
		OutRects[2] = { SX, SY, W - SX, H - SY };
		break;
	case ELayoutMode::ThreePanesBottom:
		OutRects[0] = { 0.0f, SY, W, H - SY };
		OutRects[1] = { 0.0f, 0.0f, SX, SY };
		OutRects[2] = { SX, 0.0f, W - SX, SY };
		break;
	case ELayoutMode::FourPanesLeft:
	case ELayoutMode::FourPanesRight:
	{
		const bool bLeft = Mode == ELayoutMode::FourPanesLeft;
		const float ColumnX = bLeft ? SX : 0.0f;
		const float ColumnW = bLeft ? W - SX : SX;
		OutRects[0] = bLeft ? FRect{ 0.0f, 0.0f, SX, H } : FRect{ SX, 0.0f, W - SX, H };
		for (int32 i = 0; i < 3; ++i)
		{
			const float Y0 = H * i / 3.0f, Y1 = H * (i + 1) / 3.0f;
			OutRects[i + 1] = { ColumnX, Y0, ColumnW, Y1 - Y0 };
		}
		break;
	}
	case ELayoutMode::FourPanesTop:
	case ELayoutMode::FourPanesBottom:
	{
		const bool bTop = Mode == ELayoutMode::FourPanesTop;
		const float RowY = bTop ? SY : 0.0f;
		const float RowH = bTop ? H - SY : SY;
		OutRects[0] = bTop ? FRect{ 0.0f, 0.0f, W, SY } : FRect{ 0.0f, SY, W, H - SY };
		for (int32 i = 0; i < 3; ++i)
		{
			const float X0 = W * i / 3.0f, X1 = W * (i + 1) / 3.0f;
			OutRects[i + 1] = { X0, RowY, X1 - X0, RowH };
		}
		break;
	}
	default:
		ComputeViewRects(Ratio, Size, OutRects);
		break;
	}
}

// 패널 사이 분할선. Axis가 Horizontal이면 세로선(좌우로 끌어 Horizontal 비율을 바꾼다)이다.
// 3등분된 열·행 사이 선은 끌 수 없고 경계 표시만 한다.
struct FLayoutSplitter
{
	EDragAxis Axis = EDragAxis::Horizontal;
	float Position = 0.0f;  // 세로선이면 X, 가로선이면 Y
	float Start = 0.0f;     // 선이 뻗는 구간 (세로선이면 Y 범위)
	float End = 0.0f;
	bool bDraggable = true;
};

inline int32 ComputeLayoutSplitters(ELayoutMode Mode, const FSplitRatio& Ratio, FVector2 Size, FLayoutSplitter Out[4])
{
	const float W = Size.X, H = Size.Y;
	const float SX = W * Ratio.Horizontal, SY = H * Ratio.Vertical;
	const EDragAxis V = EDragAxis::Horizontal;  // 세로선
	const EDragAxis Hz = EDragAxis::Vertical;   // 가로선
	int32 Count = 0;
	auto Add = [&](EDragAxis Axis, float Position, float Start, float End, bool bDraggable = true)
	{
		Out[Count++] = { Axis, Position, Start, End, bDraggable };
	};
	switch (Mode)
	{
	case ELayoutMode::QuadSplit:        Add(V, SX, 0, H); Add(Hz, SY, 0, W); break;
	case ELayoutMode::TwoPanes:         Add(V, SX, 0, H); break;
	case ELayoutMode::TwoPanesStacked:  Add(Hz, SY, 0, W); break;
	case ELayoutMode::ThreePanesLeft:   Add(V, SX, 0, H); Add(Hz, SY, SX, W); break;
	case ELayoutMode::ThreePanesRight:  Add(V, SX, 0, H); Add(Hz, SY, 0, SX); break;
	case ELayoutMode::ThreePanesTop:    Add(Hz, SY, 0, W); Add(V, SX, SY, H); break;
	case ELayoutMode::ThreePanesBottom: Add(Hz, SY, 0, W); Add(V, SX, 0, SY); break;
	case ELayoutMode::FourPanesLeft:
		Add(V, SX, 0, H); Add(Hz, H / 3.0f, SX, W, false); Add(Hz, H * 2.0f / 3.0f, SX, W, false); break;
	case ELayoutMode::FourPanesRight:
		Add(V, SX, 0, H); Add(Hz, H / 3.0f, 0, SX, false); Add(Hz, H * 2.0f / 3.0f, 0, SX, false); break;
	case ELayoutMode::FourPanesTop:
		Add(Hz, SY, 0, W); Add(V, W / 3.0f, SY, H, false); Add(V, W * 2.0f / 3.0f, SY, H, false); break;
	case ELayoutMode::FourPanesBottom:
		Add(Hz, SY, 0, W); Add(V, W / 3.0f, 0, SY, false); Add(V, W * 2.0f / 3.0f, 0, SY, false); break;
	default: break;
	}
	return Count;
}

class FLevelViewportLayout
{
public:

private:
};
