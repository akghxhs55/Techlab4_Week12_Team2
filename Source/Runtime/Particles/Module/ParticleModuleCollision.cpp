#include "EnginePCH.h"
#include "ParticleModuleCollision.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"
#include "Particles/ParticleTypes.h"
#include "Components/ParticleSystemComponent.h"
#include "Engine/World.h"
#include "Collision/Ray.h"
#include "Collision/HitResult.h"

UParticleModuleCollision::UParticleModuleCollision()
{
	bSpawnModule = true;
	bUpdateModule = true;

	Restitution.Mode = EDistributionMode::Constant;
	Restitution.Constant = 0.5f;
	Friction.Mode = EDistributionMode::Constant;
	Friction.Constant = 0.2f;
	RotationDamping.Mode = EDistributionMode::Constant;
	RotationDamping.Constant = 1.0f;
	DelayAmount.Mode = EDistributionMode::Constant;
	DelayAmount.Constant = 0.0f;

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleCollision::BakeDistributions()
{
	Restitution.Bake();
	Friction.Bake();
	RotationDamping.Bake();
	DelayAmount.Bake();
}

void UParticleModuleCollision::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	// 파티클마다 값을 뽑아 저장 → 같은 이펙트 안에서도 튀는 정도가 다르다
	FParticleCollisionPayload& Payload = ParticleHelper::GetPayload<FParticleCollisionPayload>(Particle, Offset);
	Payload.Restitution = Restitution.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);
	Payload.Friction = Friction.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);
	Payload.RotationDamping = RotationDamping.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);
	Payload.Delay = DelayAmount.GetFloatValue(Owner.EmitterTime, Owner.RandomStream);
	Payload.UsedCollisions = MaxCollisions;
	Payload.bInitialized = 1;
}

void UParticleModuleCollision::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
	if (DeltaTime <= 0.0f || Owner.Component == nullptr)
		return;

	UWorld* World = Owner.Component->GetWorld();
	if (World == nullptr)
		return;

	AActor* IgnoreActor = bIgnoreSourceActor ? Owner.Component->GetOwner() : nullptr;

	// 1차: 이번 프레임에 검사할 경로 전체를 감싸는 월드 상자 (반지름 포함)
	FBox SweepBounds(FVector(FLT_MAX), FVector(-FLT_MAX));
	bool bAnySweep = false;
	ParticleHelper::ForEachParticle(Owner, [&](FBaseParticle& Particle, uint8* ParticleBase)
		{
			if (Particle.Flags & STATE_Particle_IgnoreCollisions)
				return;
			if (!ParticleHelper::GetPayload<FParticleCollisionPayload>(ParticleBase, Offset).bInitialized)
				return;

			const float Radius = (std::max)(Particle.Size.X, Particle.Size.Y) * RadiusScale;
			const FVector R(Radius, Radius, Radius);
			const FVector A = Owner.SimulationToWorld(Particle.Location);
			const FVector B = Owner.SimulationToWorld(Particle.Location + Particle.Velocity * DeltaTime);
			SweepBounds.Expand(FBox(A - R, A + R));
			SweepBounds.Expand(FBox(B - R, B + R));
			bAnySweep = true;
		});
	if (!bAnySweep)
		return;

	// 후보는 에미터당 한 번만 모은다. 주변에 아무것도 없으면 파티클 검사를 통째로 건너뛴다
	CandidateCache.Reset();
	World->GatherSweepCandidates(SweepBounds, IgnoreActor, CandidateCache);
	if (CandidateCache.IsEmpty())
		return;

	// 2차: 파티클마다 후보에만 스윕
	ParticleHelper::ForEachParticle(Owner, [&](FBaseParticle& Particle, uint8* ParticleBase)
		{
			if (Particle.Flags & STATE_Particle_IgnoreCollisions)
				return;

			FParticleCollisionPayload& Payload = ParticleHelper::GetPayload<FParticleCollisionPayload>(ParticleBase, Offset);
			if (!Payload.bInitialized)
				return;

			if (Payload.Delay > 0.0f && Particle.OneOverMaxLifetime > 0.0f &&
				Particle.RelativeTime / Particle.OneOverMaxLifetime < Payload.Delay)
				return;

			// 이번 프레임에 실제로 이동할 경로 (적분은 모든 모듈 뒤에 일어난다)
			const FVector StartSim = Particle.Location;
			const FVector DeltaSim = Particle.Velocity * DeltaTime;
			const FVector StartWorld = Owner.SimulationToWorld(StartSim);
			const FVector EndWorld = Owner.SimulationToWorld(StartSim + DeltaSim);
			const FVector Segment = EndWorld - StartWorld;
			const float Length = Segment.Length();
			if (Length <= 1.0e-4f)
				return;

			const FRay Ray{ StartWorld, Segment / Length };
			const float Radius = (std::max)(Particle.Size.X, Particle.Size.Y) * RadiusScale;

			FHitResult Hit;
			if (!UWorld::SweepAgainstCandidates(CandidateCache, Ray, Length, Radius, Hit))
				return;
			// 시작부터 겹쳐 있으면 법선을 믿을 수 없다 → 이번 프레임은 건너뛴다
			if (Hit.bStartPenetrating)
				return;

			// 경로상 비율 t는 공간 변환(아핀)에 상관없이 같으므로 응답은 시뮬레이션 공간에서 계산한다
			const float T = (std::clamp)(Hit.Time, 0.0f, 1.0f);
			FVector Normal = Owner.WorldToSimulationVector(Hit.Normal).GetSafeNormal();
			const FVector HitPoint = StartSim + DeltaSim * T + Normal * SurfaceOffset;

			// 반사 전에 보고해야 이벤트의 속도·방향이 들어오던 방향이 된다.
			// 첫 충돌 = 아직 충돌 표시가 없음, 마지막 충돌 = 이번에 남은 횟수가 0이 됨
			const bool bFirstHit = (Particle.Flags & STATE_Particle_CollisionHasOccurred) == 0;
			const bool bLastHit = MaxCollisions > 0 && Payload.UsedCollisions == 1;
			Owner.ReportCollisionEvent(Particle, Owner.SimulationToWorld(HitPoint), Hit.Normal, T, Hit.HitComponent, bFirstHit, bLastHit);
			if (UParticleSystemComponent::bShowCollisions)
			{
				Owner.Component->AddDebugCollision(Owner.SimulationToWorld(HitPoint), Hit.Normal);
			}

			// V' = Vt × (1 − 마찰) − Vn × 반발계수
			const FVector Velocity = Particle.Velocity;
			const FVector Vn = Normal * Velocity.Dot(Normal);
			const FVector Vt = Velocity - Vn;
			FVector NewVelocity = Vt * (1.0f - Payload.Friction) - Vn * Payload.Restitution;

			// 튕기는 힘이 작으면 법선 성분을 없애 바닥에서 떨지 않게 한다
			const float NormalSpeed = NewVelocity.Dot(Normal);
			if (std::abs(NormalSpeed) < MinBounceSpeed)
			{
				NewVelocity -= Normal * NormalSpeed;
			}

			// 충돌은 운동 상태 자체를 바꾸므로 BaseVelocity에도 기록 (Reset이 되돌리지 않게)
			Particle.Velocity = NewVelocity;
			Particle.BaseVelocity = NewVelocity;
			Particle.RotationRate *= Payload.RotationDamping;
			Particle.BaseRotationRate *= Payload.RotationDamping;

			// 적분 후 HitPoint + V' × (1 − t)·dt 에 도착하도록 미리 당겨 둔다
			Particle.Location = HitPoint - NewVelocity * (T * DeltaTime);
			Particle.Flags |= STATE_Particle_CollisionHasOccurred;

			if (MaxCollisions <= 0)
				return;
			if (--Payload.UsedCollisions > 0)
				return;

			// 최대 충돌 횟수 도달
			auto StopAtHit = [&]()
				{
					Particle.Location = HitPoint;
					Particle.Velocity = Particle.BaseVelocity = FVector::ZeroVector;
				};
			switch (CompletionOption)
			{
			case EParticleCollisionComplete::Kill:
				// 다음 Tick의 KillParticles가 지운다. 이번 프레임은 충돌 지점에 둔다
				StopAtHit();
				Particle.RelativeTime = 1.1f;
				break;
			case EParticleCollisionComplete::Freeze:
				StopAtHit();
				Particle.RotationRate = Particle.BaseRotationRate = 0.0f;
				Particle.Flags |= STATE_Particle_Freeze;
				break;
			case EParticleCollisionComplete::HaltCollisions:
				Particle.Flags |= STATE_Particle_IgnoreCollisions;
				break;
			case EParticleCollisionComplete::FreezeTranslation:
				StopAtHit();
				Particle.Flags |= STATE_Particle_FreezeTranslation;
				break;
			case EParticleCollisionComplete::FreezeRotation:
				Particle.Flags |= STATE_Particle_FreezeRotation;
				break;
			case EParticleCollisionComplete::FreezeMovement:
				StopAtHit();
				Particle.Flags |= STATE_Particle_FreezeTranslation | STATE_Particle_FreezeRotation;
				break;
			}
		});
}
