#pragma once

struct ImFont;
class UObject;
class FProperty;
struct FRotator;

namespace FPropertyEditor
{
	// DetailsPanel.cpp의 DrawProperty를 그대로 옮긴다 (값이 바뀌면 PostEditChangeProperty 호출).
	void DrawProperty(UObject* Object, const FProperty& Property, ImFont* CustomFont = nullptr);
	// 기반 클래스부터 CPF_Edit 속성을 CollapsingHeader로 묶어 그린다 (DetailsPanel의 루프를 옮김).
	void DrawObjectProperties(UObject* Object, ImFont* CustomFont = nullptr);

	// 레벨 디테일 패널의 트랜스폼 줄과 같은 모양: 왼쪽 라벨, 오른쪽 X(빨강)/Y(초록)/Z(파랑) 버튼 + 드래그 칸.
	// 축 버튼을 누르면 ResetValue로 되돌린다. 값이 바뀌면 true.
	bool DrawVector3(const char* Label, float* Values, float ResetValue);
	// FRotator를 Roll=X, Pitch=Y, Yaw=Z 순서로 보여 준다 (레벨 디테일 패널과 같은 축 순서).
	bool DrawRotator(const char* Label, FRotator& Rotator);
}
// DetailsPanel.cpp는 이 헤더를 include하고 원래 함수 대신 FPropertyEditor::를 부르도록 바꾼다.