#include "EnginePCH.h"
#include "ParticleModuleBeamNoise.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleHelper.h"

#include <cmath>

UParticleModuleBeamNoise::UParticleModuleBeamNoise()
{
	bSpawnModule = true;
	bUpdateModule = true;
}

uint32 UParticleModuleBeamNoise::RequiredBytes() const
{
	return static_cast<uint32>(sizeof(FBeamNoiseHeader) + sizeof(FBeamNoisePoint) * (GetFrequency() + 1));
}

FBeamNoiseHeader& UParticleModuleBeamNoise::GetHeader(uint8* ParticleBase, int32 Offset)
{
	return ParticleHelper::GetPayload<FBeamNoiseHeader>(ParticleBase, Offset);
}

FBeamNoisePoint* UParticleModuleBeamNoise::GetPoints(uint8* ParticleBase, int32 Offset)
{
	return reinterpret_cast<FBeamNoisePoint*>(ParticleBase + Offset + sizeof(FBeamNoiseHeader));
}

const FBeamNoisePoint* UParticleModuleBeamNoise::GetPoints(const uint8* ParticleBase, int32 Offset)
{
	return reinterpret_cast<const FBeamNoisePoint*>(ParticleBase + Offset + sizeof(FBeamNoiseHeader));
}

void UParticleModuleBeamNoise::PickTargets(FParticleEmitterInstance& Owner, FBeamNoisePoint* Points) const
{
	const int32 N = GetFrequency();
	// 양 끝은 Source/Target에 붙어 있어야 한다
	Points[0].Target = FVector2(0.0f, 0.0f);
	Points[N].Target = FVector2(0.0f, 0.0f);
	for (int32 i = 1; i < N; ++i)
	{
		Points[i].Target = FVector2(
			Owner.RandomStream.FRandRange(-NoiseRange, NoiseRange),
			Owner.RandomStream.FRandRange(-NoiseRange, NoiseRange));
	}
}

void UParticleModuleBeamNoise::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	uint8* ParticleBase = reinterpret_cast<uint8*>(&Particle);
	FBeamNoisePoint* Points = GetPoints(ParticleBase, Offset);

	// 첫 프레임부터 휘어진 모양이 보이도록 바로 목표 위치에서 시작한다
	PickTargets(Owner, Points);
	const int32 N = GetFrequency();
	for (int32 i = 0; i <= N; ++i)
	{
		Points[i].Current = Points[i].Target;
	}
	GetHeader(ParticleBase, Offset).LockTimer = NoiseLockTime;
}

void UParticleModuleBeamNoise::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
	const int32 N = GetFrequency();
	const float MaxStep = NoiseSpeed * DeltaTime;

	ParticleHelper::ForEachParticle(Owner, [&](FBaseParticle&, uint8* ParticleBase)
		{
			FBeamNoiseHeader& Header = GetHeader(ParticleBase, Offset);
			FBeamNoisePoint* Points = GetPoints(ParticleBase, Offset);

			Header.LockTimer -= DeltaTime;
			if (Header.LockTimer <= 0.0f)
			{
				PickTargets(Owner, Points);
				Header.LockTimer = NoiseLockTime;
			}

			for (int32 i = 0; i <= N; ++i)
			{
				FBeamNoisePoint& Point = Points[i];
				if (NoiseSpeed <= 0.0f)
				{
					Point.Current = Point.Target;
					continue;
				}

				// 목표를 넘지 않게 MaxStep만큼만 다가간다
				const float DX = Point.Target.X - Point.Current.X;
				const float DY = Point.Target.Y - Point.Current.Y;
				const float Dist = std::sqrt(DX * DX + DY * DY);
				if (Dist <= MaxStep || Dist < 1e-6f)
				{
					Point.Current = Point.Target;
				}
				else
				{
					const float Scale = MaxStep / Dist;
					Point.Current = FVector2(Point.Current.X + DX * Scale, Point.Current.Y + DY * Scale);
				}
			}
		});
}
