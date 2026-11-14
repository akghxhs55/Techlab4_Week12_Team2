#include "EnginePCH.h"
#include "ParticleModuleSubUV.h"
#include "ParticleModuleRequired.h"
#include "Particles/ParticleEmitterInstance.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleHelper.h"

UParticleModuleSubUV::UParticleModuleSubUV()
{
	bSpawnModule = true;
	bUpdateModule = true;

	// 마지막 키 Value를 (칸 수 - 1)로 맞춰야 수명 동안 전체 칸을 돈다
	SubImageIndex.Mode = EDistributionMode::Curve;
	FFloatCurveKey Start;
	Start.Time = 0.0f;
	Start.Value = 0.0f;
	FFloatCurveKey End;
	End.Time = 1.0f;
	End.Value = 3.0f;
	SubImageIndex.CurveKeys.Add(Start);
	SubImageIndex.CurveKeys.Add(End);

	// Build() 전에도 기본값이 동작하도록
	BakeDistributions();
}

void UParticleModuleSubUV::BakeDistributions()
{
	SubImageIndex.Bake();
}

float UParticleModuleSubUV::DetermineImageIndex(FParticleEmitterInstance& Owner, const UParticleModuleRequired& Required,
	const FBaseParticle& Particle, FFullSubUVPayload& Payload) const
{
	const EParticleSubUVInterpMethod Method = Required.GetSubUVMethod();
	const int32 Total = Required.GetTotalSubImages();
	float ImageIndex = Payload.ImageIndex;

	if (Method == EParticleSubUVInterpMethod::Linear || Method == EParticleSubUVInterpMethod::Linear_Blend)
	{
		ImageIndex = SubImageIndex.GetFloatValue(Particle.RelativeTime, Owner.RandomStream);
	}
	else if (Method == EParticleSubUVInterpMethod::Random || Method == EParticleSubUVInterpMethod::Random_Blend)
	{
		const float Interval = Required.GetRandomImageTime();
		if (Interval == 0.0f || Payload.RandomImageTime == 0.0f
			|| Particle.RelativeTime - Payload.RandomImageTime > Interval)
		{
			ImageIndex = static_cast<float>(Owner.RandomStream.RandRange(0, Total - 1));
			// 0이면 "아직 안 뽑음"으로 읽히므로 생성 직후에도 0이 아닌 값으로
			Payload.RandomImageTime = Particle.RelativeTime > 0.0f ? Particle.RelativeTime : 1e-4f;
		}
	}

	// 마지막 칸을 넘지 않게. Blend가 아니면 소수부를 버린다
	const float MaxIndex = static_cast<float>(Total - 1);
	ImageIndex = ImageIndex < 0.0f ? 0.0f : (ImageIndex > MaxIndex ? MaxIndex : ImageIndex);
	if (Method == EParticleSubUVInterpMethod::Linear || Method == EParticleSubUVInterpMethod::Random)
	{
		ImageIndex = FMath::TruncToFloat(ImageIndex);
	}
	return ImageIndex;
}

void UParticleModuleSubUV::Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle)
{
	FFullSubUVPayload& Payload = ParticleHelper::GetPayload<FFullSubUVPayload>(Particle, Offset);
	Payload.ImageIndex = 0.0f;
	Payload.RandomImageTime = 0.0f;

	const UParticleModuleRequired* Required = Owner.CurrentLODLevel->GetRequiredModule();
	if (Required && Required->UsesSubUV())
	{
		Payload.ImageIndex = DetermineImageIndex(Owner, *Required, Particle, Payload);
	}
}

void UParticleModuleSubUV::Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime)
{
	const UParticleModuleRequired* Required = Owner.CurrentLODLevel->GetRequiredModule();
	if (!Required || !Required->UsesSubUV())
		return;

	ParticleHelper::ForEachParticle(Owner, [&](FBaseParticle& Particle, uint8* ParticleBase)
		{
			FFullSubUVPayload& Payload = ParticleHelper::GetPayload<FFullSubUVPayload>(ParticleBase, Offset);
			Payload.ImageIndex = DetermineImageIndex(Owner, *Required, Particle, Payload);
		});
}