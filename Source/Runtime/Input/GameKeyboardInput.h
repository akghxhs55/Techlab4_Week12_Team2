#pragma once

#include "Input/InputSystem.h"

// PIE and standalone play must forward the same movement keys, including releases.
// Escape is handled separately by each host's play-session controls.
template <typename TReceiver>
void DispatchGameKeyboardInput(TReceiver&& Receive)
{
	constexpr EKeyCode Keys[] = {
		EKeyCode::W, EKeyCode::A, EKeyCode::S, EKeyCode::D,
		EKeyCode::Q, EKeyCode::E, EKeyCode::Space, EKeyCode::Shift
	};
	for (EKeyCode Key : Keys)
	{
		if (FInputSystem::IsKeyPressed(Key)) Receive(static_cast<int32>(Key), true);
		if (FInputSystem::IsKeyReleased(Key)) Receive(static_cast<int32>(Key), false);
	}
}
