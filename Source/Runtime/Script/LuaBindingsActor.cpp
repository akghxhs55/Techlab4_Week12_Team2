#include "EnginePCH.h"
#include "LuaBindingsInternal.h"

#include "Components/MovementComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SpringArmComponent.h"
#include "Script/LuaActorRef.h"
#include "Engine/World.h"
#include "Engine/Level.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"

namespace
{
	UMovementComponent* FindFirstMovement(AActor& Actor)
	{
		for (UActorComponent* Comp : Actor.GetComponents())
		{
			if (UMovementComponent* MoveComp = Cast<UMovementComponent>(Comp))
			{
				return MoveComp;
			}
		}
		return nullptr;
	}

	USpringArmComponent& CameraArm(const FLuaActorRef& Ref)
	{
		for (UActorComponent* Comp : Deref(Ref).GetComponents())
			if (auto* Arm = Cast<USpringArmComponent>(Comp)) return *Arm;
		throw sol::error("Actor does not have a SpringArmComponent.");
	}
}

namespace LuaBindingsDetail
{
	// FVector, FRotator
	void RegisterMath(sol::state& Lua)
	{
		Lua.new_usertype<FVector>("FVector",
			sol::call_constructor,
			sol::constructors<FVector(), FVector(float, float, float)>(),
			"x", &FVector::X,
			"y", &FVector::Y,
			"z", &FVector::Z,
			sol::meta_function::addition, [](const FVector& A, const FVector& B) { return A + B; },
			sol::meta_function::subtraction, [](const FVector& A, const FVector& B) { return A - B; },
			sol::meta_function::unary_minus, [](const FVector& A) { return -A; },

			sol::meta_function::multiplication, sol::overload(
				[](const FVector& V, float Scalar) { return V * Scalar; },
				[](const float Scalar, const FVector& V) { return V * Scalar; }
			),
			sol::meta_function::division, [](const FVector& V, float Scalar) { return V / Scalar; },
			sol::meta_function::equal_to, [](const FVector& A, const FVector& B) { return A == B; },
			sol::meta_function::to_string, [](const FVector& V)
			{ return std::format("({:.2f}, {:.2f}, {:.2f})", V.X, V.Y, V.Z); },

			"Length", &FVector::Length,
			"Normalize", [](const FVector& V) { return V.GetSafeNormal(); },
			"Dot", [](const FVector& A, const FVector& B) { return A.Dot(B); },
			"Cross", [](const FVector& A, const FVector& B) { return A.Cross(B); }
		);

		Lua.new_usertype<FRotator>("FRotator",
			sol::call_constructor,
			sol::constructors<FRotator(), FRotator(float, float, float)>(),
			"pitch", &FRotator::Pitch,
			"yaw", &FRotator::Yaw,
			"roll", &FRotator::Roll,

			"LookDirection", &FRotator::LookDirection,
			sol::meta_function::to_string, [](const FRotator& R)
			{ return std::format("(P={:.1f}, Y={:.1f}, R={:.1f})", R.Pitch, R.Yaw, R.Roll); }
		);
	}

	void RegisterActor(sol::state& Lua)
	{
		Lua.new_usertype<FLuaActorRef>("Actor",
			sol::no_constructor, // 액터는 Lua에서 생성할 수 없음

			"PlayAudio", [](const FLuaActorRef& Ref)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (UAudioComponent* Audio = Cast<UAudioComponent>(Component)) { Audio->Play(); return; }
			},
			"StopAudio", [](const FLuaActorRef& Ref)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (UAudioComponent* Audio = Cast<UAudioComponent>(Component)) { Audio->Stop(); return; }
			},
			"UUID", sol::property([](const FLuaActorRef& Ref) { return Ref.UUID; }),
			"IsValid", [](const FLuaActorRef& Ref) { return Ref.Get() != nullptr; },
			"Name", sol::property([](const FLuaActorRef& Ref) { return Deref(Ref).GetName(); }),
			"HasComponent", [](const FLuaActorRef& Ref, const FString& ClassName)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
				{
					if (!Component || !Component->IsRegistered()) continue;
					for (UClass* Class = Component->GetClass(); Class; Class = Class->Super)
						if (Class->Name == ClassName) return true;
				}
				return false;
			},
			"GetComponentRelativeScale", [](const FLuaActorRef& Ref, const FString& Name) -> sol::optional<FVector>
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (Component && Component->GetName() == Name)
						if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
							return SceneComponent->GetRelativeScale3D();
				return sol::nullopt;
			},
			"SetComponentRelativeScale", [](const FLuaActorRef& Ref, const FString& Name, const FVector& Scale)
			{
				for (UActorComponent* Component : Deref(Ref).GetComponents())
					if (Component && Component->GetName() == Name)
						if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
						{
							SceneComponent->SetRelativeScale3D(Scale);
							return true;
						}
				return false;
			},
			"GetOverlappingActors", [](const FLuaActorRef& Ref, sol::this_state State)
			{
				TArray<AActor*> Actors;
				Deref(Ref).GetOverlappingActors(Actors);
				sol::state_view Lua(State);
				sol::table Result = Lua.create_table();
				for (int32 Index = 0; Index < Actors.Num(); ++Index)
					Result[Index + 1] = FLuaActorRef::Make(Actors[Index]);
				return Result;
			},
			"Location", sol::property(
				// Getter
				[](const FLuaActorRef& Ref) { return Deref(Ref).GetActorLocation(); },
				// Setter
				[](const FLuaActorRef& Ref, const FVector& V)
				{
					if (USceneComponent* RootComp = Deref(Ref).GetRootComponent())
					{
						RootComp->SetWorldLocation(V, false);
					}
				}),
			"Rotation", sol::property(
				// Getter
				[](const FLuaActorRef& Ref) { return Deref(Ref).GetActorRotation(); },
				// Setter
				[](const FLuaActorRef& Ref, const FRotator& R)
				{
					if (USceneComponent* RootComp = Deref(Ref).GetRootComponent())
					{
						RootComp->SetWorldRotation(R, false);
					}
				}),
			// Velocity는 MovementComponent가 있는 경우에만 제공
			"Velocity", sol::property(
				// Getter
				[](const FLuaActorRef& Ref)
				{
					if (UMovementComponent* MoveComp = FindFirstMovement(Deref(Ref)))
					{
						return MoveComp->Velocity;
					}
					return FVector::ZeroVector;
				},
				// Setter
				[](const FLuaActorRef& Ref, const FVector& V)
				{
					if (UMovementComponent* MoveComp = FindFirstMovement(Deref(Ref)))
					{
						MoveComp->Velocity = V;
					}
					else
					{
						throw sol::error("Actor does not have a MovementComponent to set Velocity.");
					}
				}),
			"PrintLocation", [](const FLuaActorRef& Ref)
			{
				const FVector Location = Deref(Ref).GetActorLocation();
				KRS_LOG(Info, "[Lua] {} ({:.2f}, {:.2f}, {:.2f})", Deref(Ref).GetName(), Location.X, Location.Y, Location.Z);
			},

			"Destroy", [](const FLuaActorRef& Ref)
			{
				// 이미 파괴되지 않은 액터만 PendingDestroies에 추가
				if (Ref.Get())
				{
					AddPendingDestroy(Ref);
				}
			},

			sol::meta_function::equal_to, [](const FLuaActorRef& A, const FLuaActorRef& B)
			{
				return A.UUID == B.UUID;
			},
			sol::meta_function::to_string, [](const FLuaActorRef& Ref)
			{
				return std::format("Actor#{}", Ref.UUID);
			},

			"CameraLagEnabled", sol::property(
				[](const FLuaActorRef& Ref)
				{
					return CameraArm(Ref).bEnableCameraLag;
				},
				[](const FLuaActorRef& Ref, bool b)
				{
					CameraArm(Ref).bEnableCameraLag = b;
				}
			),
			"ResetCameraLag", [](const FLuaActorRef& Ref)
			{
				CameraArm(Ref).ResetCameraLag();
			}
		);

		Lua.set_function("GetActorByName", [](sol::this_environment Env, const std::string& Name) -> sol::optional<FLuaActorRef>
			{
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("GetActorByName needs an Actor script context.");
				UWorld* World = Owner->GetWorld();
				if (!World) throw sol::error("GetActorByName needs an Actor script context.");
				for (AActor* Actor : World->GetCurrentLevel()->GetActors())
				{
					if (Actor && Actor->GetName() == Name)
					{
						return FLuaActorRef::Make(Actor);
					}
				}
				return sol::nullopt;
			});
		Lua.set_function("GetComponentByName", [](sol::this_environment Env, const std::string& Name) -> sol::optional<FLuaComponentRef>
			{
				AActor* Owner = OwnerFromEnv(Env);
				if (!Owner) throw sol::error("GetComponentByName needs an Actor script context.");
				for (UActorComponent* Component : Owner->GetComponents())
				{
					if (Component && Component->GetName() == Name)
					{
						return FLuaComponentRef::Make(Component);
					}
				}
				return sol::nullopt;
			});

		Lua.set_function("SetViewTargetWithBlend",
			[](sol::this_environment Env,
				const FLuaActorRef& Target,
				sol::optional<float> Time,
				sol::optional<std::string> Function,
				sol::optional<float> Exp) -> bool
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;
				APlayerController* PC = World ? World->GetPlayerController() : nullptr;
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				AActor* TargetActor = Target.Get();
				if (!PCM || !TargetActor || TargetActor->GetWorld() != World)
					return false;

				FViewTargetTransitionParams Params;
				Params.BlendTime = Time.value_or(0.0f);
				Params.BlendExp = Exp.value_or(2.0f);

				const std::string Mode = Function.value_or("Cubic");

				if (Mode == "Linear")
					Params.BlendFunction = EViewTargetBlendFunction::Linear;
				else if (Mode == "EaseIn")
					Params.BlendFunction = EViewTargetBlendFunction::EaseIn;
				else if (Mode == "EaseOut")
					Params.BlendFunction = EViewTargetBlendFunction::EaseOut;
				else if (Mode == "EaseInOut")
					Params.BlendFunction = EViewTargetBlendFunction::EaseInOut;
				else if (Mode != "Cubic")
					return false;

				PCM->SetViewTarget(TargetActor, Params);
				return true;
			});

		Lua.set_function("IsCameraBlending",
			[](sol::this_environment Env) -> bool
			{
				AActor* Owner = OwnerFromEnv(Env);
				UWorld* World = Owner ? Owner->GetWorld() : nullptr;
				APlayerController* PC = World ? World->GetPlayerController() : nullptr;
				APlayerCameraManager* PCM = PC ? PC->GetCameraManager() : nullptr;

				if (!PCM)
					throw sol::error("IsCameraBlending: camera manager is unavailable.");

				return PCM->IsBlending();
			});
		Lua.set_function("SetCameraRelativeTransform",
			[](const FLuaActorRef& Ref,
				const FVector& RelativeLocation,
				const FRotator& RelativeRotation)
			{
				AActor& Actor = Deref(Ref);

				for (UActorComponent* Component : Actor.GetComponents())
				{
					if (UCameraComponent* Camera = Cast<UCameraComponent>(Component))
					{
						Camera->SetRelativeLocationAndRotation(
							RelativeLocation,
							RelativeRotation
						);
						return;
					}
				}

				throw sol::error(
					"SetCameraRelativeTransform: Actor has no camera component."
				);
			});
	}

	void RegisterComponent(sol::state& Lua)
	{
		Lua.new_usertype<FLuaComponentRef>("Component",
			sol::no_constructor,
			"IsValid", [](const FLuaComponentRef& R) { return R.Get() != nullptr; },
			"Name", sol::property([](const FLuaComponentRef& R) { return Deref(R).GetName(); }),
			"Owner", sol::property([](const FLuaComponentRef& R) { return FLuaActorRef::Make(Deref(R).GetOwner()); }),
			"Location", sol::property(
				// Getter
				[](const FLuaComponentRef& Ref)
				{
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						return SceneComp->GetRelativeLocation();
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				},
				// Setter
				[](const FLuaComponentRef& Ref, const FVector& V)
				{
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						SceneComp->SetRelativeLocation(V);
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				}),
			"Rotation", sol::property(
				// Getter
				[](const FLuaComponentRef& Ref) {
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						return SceneComp->GetRelativeRotation();
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				},
				// Setter
				[](const FLuaComponentRef& Ref, const FRotator& R)
				{
					if (auto* SceneComp = Cast<USceneComponent>(&Deref(Ref)))
					{
						SceneComp->SetRelativeRotation(R);
					}
					else
					{
						throw sol::error("Component is not a scene component.");
					}
				})
		);
	}
}
