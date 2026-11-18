#include "EnginePCH.h"
#include "GameFramework/MyPlayerController.h"
#include "GameFramework/Actor/MyCharacter.h"
#include "GameFramework/Actor/KurisuCharacter.h"

void AMyPlayerController::InputKey(int32 Key, bool bDown)
{
	// Super가 bKeyDown을 갱신하므로, 눌림 순간 판정은 호출 전에 이전 상태를 잡아 둔다
	const bool bJustPressed = bDown && !IsKeyDown(Key);
	Super::InputKey(Key, bDown);

	if (!bJustPressed) return;

	if (Key == static_cast<int32>(EKeyCode::LButton))
	{
		// Kurisu는 좌클릭 잽, 다른 캐릭터는 기존 공격
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
		{
			Kurisu->Jab();
		}
		else if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Attack();
		}
	}

	if (Key == static_cast<int32>(EKeyCode::RButton))
	{
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
		{
			Kurisu->Punch();
		}
	}

	if (Key == static_cast<int32>(EKeyCode::Space))
	{
		if (AMyCharacter* MyCharacter = Cast<AMyCharacter>(GetPawn()))
		{
			MyCharacter->Jump();
		}
	}

	// Kurisu 슬라이드: E / 구르기: R. Shift(Run)는 캐릭터 Tick이 키 상태를 읽는다
	if (Key == static_cast<int32>(EKeyCode::E))
	{
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
			Kurisu->Slide();
	}

	if (Key == static_cast<int32>(EKeyCode::R))
	{
		if (AKurisuCharacter* Kurisu = Cast<AKurisuCharacter>(GetPawn()))
			Kurisu->Roll();
	}
}
