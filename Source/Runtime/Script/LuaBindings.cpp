#include "EnginePCH.h"
#include "LuaBindings.h"
#include "LuaBindingsInternal.h"

#include "GameFramework/Actor.h"
#include "Script/LuaActorRef.h"
#include "Script/LuaWidgetRef.h"
#include "Script/CoroutineScheduler.h"
#include "Engine/World.h"
#include "Engine/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include "UI/UserWidget.h"
#include "UI/Button.h"
#include "UI/TextBlock.h"
#include "Asset/AssetManager.h"
#include "Audio/SoundWave.h"

namespace
{
	struct FLuaClickBinding { AActor* Owner; FLuaWidgetRef Button; };
	TArray<FLuaClickBinding> ClickBindings;
	TArray<FLuaActorRef> PendingDestroies;
}

namespace LuaBindingsDetail
{
	AActor* OwnerFromEnv(sol::this_environment& ThisEnv)
	{
		if (!ThisEnv)
			return nullptr;

		sol::environment& Env = ThisEnv;
		sol::optional<FLuaActorRef> Ref = Env["obj"];
		return Ref
			? Ref->Get()
			: nullptr;
	}

	UWorld* WorldFromEnv(sol::this_environment& ThisEnv, const char* FuncName)
	{
		AActor* Owner = OwnerFromEnv(ThisEnv);
		UWorld* World = Owner ? Owner->GetWorld() : nullptr;
		if (!World)
			throw sol::error(FString(FuncName) + " must be called from an actor script.");
		return World;
	}

	void AddPendingDestroy(const FLuaActorRef& Ref)
	{
		PendingDestroies.Add(Ref);
	}

	void TrackClickBinding(AActor* Owner, const FLuaWidgetRef& Button)
	{
		ClickBindings.Add({ Owner, Button });
	}

	void RegisterAudio(sol::state& Lua)
	{
		Lua.set_function("PlaySound2D", [](sol::this_environment ThisEnv, const FString& Path,
			sol::optional<float> Volume, sol::optional<float> Pitch)
			{
				if (USoundWave* Sound = UAssetManager::Get().LoadSound(Path))
					UGameplayStatics::PlaySound2D(WorldFromEnv(ThisEnv, "PlaySound2D"), Sound,
						Volume.value_or(1.0f), Pitch.value_or(1.0f));
			});

		Lua.set_function("PlaySoundAtLocation", [](sol::this_environment ThisEnv, const FString& Path,
			const FVector& Location, sol::optional<float> Volume, sol::optional<float> Pitch)
			{
				if (USoundWave* Sound = UAssetManager::Get().LoadSound(Path))
					UGameplayStatics::PlaySoundAtLocation(WorldFromEnv(ThisEnv, "PlaySoundAtLocation"), Sound,
						Location, Volume.value_or(1.0f), Pitch.value_or(1.0f));
			});
	}

	void RegisterGameplay(sol::state& Lua)
	{
		// 실제 전환은 다음 Tick 시작. 호출한 뒤의 코드도 이번 프레임에는 그대로 실행된다.
		Lua.set_function("OpenLevel", [](sol::this_environment ThisEnv, const FString& LevelName) {
			UGameplayStatics::OpenLevel(WorldFromEnv(ThisEnv, "OpenLevel"), LevelName);
			});
		Lua.set_function("SetGameValue", [](sol::this_environment ThisEnv, const FString& Key, double Value) {
			if (UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldFromEnv(ThisEnv, "SetGameValue")))
				GameInstance->SetValue(Key, Value);
			});
		Lua.set_function("GetGameValue", [](sol::this_environment ThisEnv, const FString& Key, sol::optional<double> Default) {
			const double Fallback = Default.value_or(0.0);
			UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldFromEnv(ThisEnv, "GetGameValue"));
			return GameInstance ? GameInstance->GetValue(Key, Fallback) : Fallback;
			});
	}

	void RegisterWidget(sol::state& Lua)
	{
		Lua.new_usertype<FLuaWidgetRef>("Widget", sol::no_constructor,
			"IsValid", [](const FLuaWidgetRef& R) { return R.Get() != nullptr; },
			"Name", sol::property([](const FLuaWidgetRef& R) { return DerefWidget<UWidget>(R).WidgetName; }),
			"Visible", sol::property(
				[](const FLuaWidgetRef& R) { return DerefWidget<UWidget>(R).IsVisible(); },
				[](const FLuaWidgetRef& R, bool b) { DerefWidget<UWidget>(R).Visibility = b ? 0 : 1; }),
			"Find", [](const FLuaWidgetRef& R, const std::string& Name) -> sol::optional<FLuaWidgetRef>
			{
				UWidget* Found = DerefWidget<UUserWidget>(R).FindWidget(Name);
				return Found ? sol::optional(FLuaWidgetRef::Make(Found)) : sol::nullopt;
			},
			"AddToViewport", [](const FLuaWidgetRef& R, sol::optional<int> Z) { DerefWidget<UUserWidget>(R).AddToViewport(Z.value_or(0)); },
			"RemoveFromViewport", [](const FLuaWidgetRef& R) { DerefWidget<UUserWidget>(R).RemoveFromViewport(); },
			"Destroy", [](const FLuaWidgetRef& R) { DerefWidget<UUserWidget>(R).Destroy(); },
			"SetText", [](const FLuaWidgetRef& R, const std::string& T) { DerefWidget<UTextBlock>(R).SetText(T); },
			"BindClick", [](sol::this_environment Env, const FLuaWidgetRef& R, sol::protected_function Fn)
			{
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("BindClick needs an Actor script context.");
				UButton& Button = DerefWidget<UButton>(R);
				const FLuaActorRef OwnerRef = FLuaActorRef::Make(Owner);
				Button.OnClicked = [OwnerRef, Fn]
					{
						if (!OwnerRef.Get()) return;              // 액터가 이미 지워졌다
						sol::protected_function_result Result = Fn();
						if (!Result.valid())
						{
							sol::error Err = Result;
							KRS_LOG(Error, "[Lua] OnClicked: {}", Err.what());
						}
					};
				TrackClickBinding(Owner, R);
			});

		Lua.set_function("CreateWidget", [](sol::this_environment Env, const std::string& Path) -> sol::optional<FLuaWidgetRef>
			{
				AActor* Owner = OwnerFromEnv(Env);
				UUserWidget* W = Owner ? ::CreateWidget(Owner->GetWorld(), Path) : nullptr;
				return W ? sol::optional(FLuaWidgetRef::Make(W)) : sol::nullopt;
			});
	}

	void RegisterCoroutine(sol::state& Lua)
	{
		Lua.set_function("StartCoroutine",
			[](sol::this_environment ThisEnv, sol::function Fn)
			{
				UWorld* World = WorldFromEnv(ThisEnv, "StartCoroutine");
				World->GetCoroutineScheduler().Start(OwnerFromEnv(ThisEnv), Fn);
			});

		// C++ 함수가 반환값을 yield 값으로 남기고 멈춘다. 코루틴 밖에서 부르면 Lua 에러
		Lua.set_function("wait", sol::yielding(
			[](float Seconds)
			{
				return std::make_tuple("time", Seconds);
			}));
		Lua.set_function("wait_frame", sol::yielding(
			[]()
			{
				return;
			}));
		Lua.set_function("wait_until", sol::yielding(
			[](sol::function Cond)
			{
				return std::make_tuple("until", Cond);
			}));
		Lua.set_function("wait_until_move_done", sol::yielding(
			[]()
			{
				return "move";
			}));

		Lua.set_function("move_to",
			[](sol::this_environment ThisEnv, const FVector& Target, float Speed)
			{
				UWorld* World = WorldFromEnv(ThisEnv, "move_to");
				World->GetCoroutineScheduler().MoveTo(OwnerFromEnv(ThisEnv), Target, Speed);
			});
	}
}

namespace FLuaBindings
{
	void Register(sol::state& Lua)
	{
		using namespace LuaBindingsDetail;
		// TODO(B): wait, wait_until, wait_until_move_done, StartCoroutine
		RegisterMath(Lua);
		RegisterActor(Lua);
		RegisterComponent(Lua);
		RegisterInput(Lua);
		RegisterCoroutine(Lua);
		RegisterWidget(Lua);
		RegisterGameplay(Lua);
		RegisterAudio(Lua);
		RegisterCameraAction(Lua);
		RegisterAnimation(Lua);
	}

	void FlushPendingDestroy(UWorld* World)
	{
		TArray<FLuaActorRef> ToDestroy = std::move(PendingDestroies);
		PendingDestroies = TArray<FLuaActorRef>();

		for (const FLuaActorRef& Ref : ToDestroy)
		{
			if (Ref.World != World)
			{
				continue;
			}
			if (AActor* Actor = Ref.Get()) // 살아 있는 액터만 Destroy
			{
				Actor->Destroy();
			}
		}
	}

	void ReleaseCallbacks(AActor* Owner)
	{
		for (int32 i = ClickBindings.Num() - 1; i >= 0; --i)
		{
			if (ClickBindings[i].Owner != Owner) continue;
			if (UButton* B = Cast<UButton>(ClickBindings[i].Button.Get()))
				B->OnClicked = nullptr;
			ClickBindings.RemoveAt(i);
		}

		LuaBindingsDetail::ReleaseAnimGraphBindings(Owner);
	}
}
