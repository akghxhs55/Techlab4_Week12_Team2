#include "EnginePCH.h"
#include "LuaBindingsInternal.h"

#include "Core/EngineTimer.h"
#include "GameFramework/Actor.h"
#include "Input/InputSystem.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraShakeBase.h"

namespace
{
	EKeyCode ToKeyCode(const std::string& Name)
	{
		if (Name.size() == 1 && std::isalnum(static_cast<unsigned char>(Name[0])))
		{
			// 알파벳 또는 숫자 키를 EKeyCode로 변환
			return static_cast<EKeyCode>(std::toupper(static_cast<unsigned char>(Name[0])));
		}

		static const TMap<std::string, EKeyCode> KeyMap =
		{
			{ "space", EKeyCode::Space },
			{ "enter", EKeyCode::Enter },
			{ "escape", EKeyCode::Escape },
			{ "tab", EKeyCode::Tab },
			{ "shift", EKeyCode::Shift },
			{ "backspace", EKeyCode::Backspace },
			{ "ctrl", EKeyCode::Control },
			{ "alt", EKeyCode::Alt },
			{ "left", EKeyCode::Left },
			{ "right", EKeyCode::Right },
			{ "up", EKeyCode::Up },
			{ "down", EKeyCode::Down },
		};

		std::string Lower = Name;
		std::transform(Lower.begin(), Lower.end(), Lower.begin(), [](unsigned char c) { return std::tolower(c); });
		if (const EKeyCode* Code = KeyMap.Find(Lower))
		{
			return *Code;
		}
		throw sol::error("Unknown key name: " + Name);
	}
}

namespace LuaBindingsDetail
{
	void RegisterInput(sol::state& Lua)
	{
		Lua.set_function("IsKeyDown", [](const std::string& K) {
			return FInputSystem::IsKeyDown(ToKeyCode(K));
			});
		Lua.set_function("IsKeyPressed", [](const std::string& K) {
			return FInputSystem::IsKeyPressed(ToKeyCode(K));
			});
		Lua.set_function("IsKeyReleased", [](const std::string& K) {
			return FInputSystem::IsKeyReleased(ToKeyCode(K));
			});

		Lua.set_function("IsLeftMouseButtonPressed", []() {
			return FInputSystem::IsMousePressed(EMouseButton::Left);
			});

		Lua.set_function("IsRightMouseButtonPressed", []() {
			return FInputSystem::IsMousePressed(EMouseButton::Right);
			});

		// Viewport 포지션을 반환한다.
		Lua.set_function("GetMousePosition", [](sol::this_environment ThisEnv) {
			AActor* Owner = OwnerFromEnv(ThisEnv);
			if (!Owner)
			{
				throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
			}

			UWorld* World = Owner->GetWorld();
			if (!World)
			{
				throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
			}

			FWorldContext* Context =
				GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;

			if (!Context || !Context->GameViewport)
			{
				throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
			}

			FGameViewportClient* Viewport = Context->GameViewport.get();

			const FRect& ViewportRect = Viewport->GetRect();

			//const float MouseX = FInputSystem::GetMouseX();
			//const float MouseY = FInputSystem::GetMouseY();
			POINT Mouse{};
			if (!::GetCursorPos(&Mouse))
			{
				throw sol::error("Failed to get mouse cursor position.");
			}

			const float LocalX = static_cast<float>(Mouse.x) - ViewportRect.X;
			const float LocalY = static_cast<float>(Mouse.y) - ViewportRect.Y;

			return std::make_tuple(
				LocalX,
				LocalY);
			});
		Lua.set_function("GetViewportSize",
			[](sol::this_environment ThisEnv) -> std::tuple<float, float>
			{
				AActor* Owner = OwnerFromEnv(ThisEnv);
				if (!Owner)
				{
					throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
				}

				UWorld* World = Owner->GetWorld();
				if (!World)
				{
					throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
				}

				FWorldContext* Context =
					GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;

				if (!Context || !Context->GameViewport)
				{
					throw sol::error("GetMousePosition must be called from a coroutine with an Actor context.");
				}

				FGameViewportClient* Viewport = Context->GameViewport.get();

				const FRect& ViewportRect = Viewport->GetRect();

				return  std::make_tuple(
					static_cast<float>(ViewportRect.Width),
					static_cast<float>(ViewportRect.Height));
			}
		);

		Lua.set_function("ScreenToWorldRay",
			[](sol::this_environment Env, float X, float Y)
			-> std::tuple<sol::optional<FVector>, sol::optional<FVector>>
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;

				if (!GEngine || !World)
				{
					return {};
				}

				FWorldContext* Context =
					GEngine->GetWorldContextFromWorld(World);

				if (!Context || !Context->GameViewport)
				{
					return {};
				}

				FRay Ray;
				if (!Context->GameViewport->ScreenToWorldRay(X, Y, &Ray))
				{
					return {};
				}

				return {
					sol::optional<FVector>(Ray.Origin),
					sol::optional<FVector>(Ray.Direction)
				};
			}
		);

		Lua.set_function("Raycast",
			[](sol::this_environment Env,
				const FVector& Origin,
				const FVector& Direction) -> sol::optional<FVector>
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;

				if (!World || Direction.Dot(Direction) <= 0.0000001f)
				{
					return sol::nullopt;
				}

				FRay Ray{
					Origin,
					Direction.Normalized()
				};

				FHitResult Hit;
				if (!World->LineTraceSingle(Ray, Hit))
				{
					return sol::nullopt;
				}

				return Hit.ImpactPoint;
			});

		// Tick(dt)의 dt를 쓰는 게 원칙. 코루틴처럼 dt를 못 받는 곳에서만 쓴다
		Lua.set_function("GetDeltaTime", []() {
			return EngineTimer::GetDeltaTime();
			});
		Lua.set_function("GetTime", []() {
			return EngineTimer::GetTotalTime();
			});
	}

	void RegisterCameraAction(sol::state& Lua)
	{
		Lua.set_function("StartHitStop",
			[](sol::this_environment Env, float Duration, float Dilation)
			{
				WorldFromEnv(Env, "StartHitStop")->StartHitStop(Duration, Dilation);
			});

		Lua.set_function("StartCameraShake",
			[](sol::this_environment Env, float Scale) -> bool
			{
				UWorld* World = WorldFromEnv(Env, "StartCameraShake");
				APlayerController* PC = World->GetPlayerController();
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				if (!PCM) return false;

				UClass* ShakeClass = UCameraShakeBase::StaticClass();
				auto* Settings = static_cast<UCameraShakeBase*>(
					ShakeClass->GetDefaultObject());

				if (!Settings) return false;

				// Lua에서는 Scale로 세기를 조절한다.
				Settings->Duration = 1.0f;
				Settings->BlendInTime = 0.05f;
				Settings->BlendOutTime = 0.3f;
				Settings->RotationPitch.Amplitude = 2.0f;
				Settings->RotationPitch.Frequency = 10.0f;
				Settings->RotationYaw.Amplitude = 1.0f;
				Settings->RotationYaw.Frequency = 13.0f;

				return PCM->StartCameraShake(ShakeClass, Scale) != nullptr;
			});

		Lua.set_function("StopAllCameraShakes",
			[](sol::this_environment Env, bool bImmediately)
			{
				UWorld* World = WorldFromEnv(Env, "StopAllCameraShakes");
				APlayerController* PC = World->GetPlayerController();
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				if (PCM)
				{
					PCM->StopAllCameraShakes(bImmediately);
				}
			});
	}
}
