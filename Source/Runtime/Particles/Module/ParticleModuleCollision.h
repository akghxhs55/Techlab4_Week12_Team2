#pragma once

#include "Core/Types.h"
#include "Particles/ParticleModule.h"
#include "Particles/Distributions.h"
#include "Containers/Array.h"
#include "ParticleModuleCollision.generated.h"

class FPrimitiveSceneProxy;

// 최대 충돌 횟수에 도달했을 때 할 일
UENUM()
enum class EParticleCollisionComplete : uint8
{
	Kill,               // 제거
	Freeze,             // 제자리 정지 (이동·회전·모듈 갱신 모두 멈춤)
	HaltCollisions,     // 충돌 검사만 끄고 계속 이동
	FreezeTranslation,  // 이동만 정지
	FreezeRotation,     // 회전만 정지
	FreezeMovement,     // 이동 + 회전 정지 (모듈 갱신은 계속)
};

// 파티클마다 다른 충돌 상태. Spawn에서 채운다
struct FParticleCollisionPayload
{
	float  Restitution;        // 반발 계수 (법선 성분 배율)
	float  Friction;           // 접선 성분 감쇠
	float  RotationDamping;    // 충돌마다 회전 속도에 곱함
	float  Delay;              // 이 나이(초) 전에는 충돌하지 않음
	int32  UsedCollisions;     // 남은 충돌 횟수. MaxCollisions가 0이면 무시
	uint32 bInitialized;       // 이 모듈이 꺼진 LOD에서 태어난 파티클은 0 → 검사하지 않음
};

// 이번 프레임 이동 경로를 월드에 스윕해서 닿으면 반사한다.
// 모든 속도 변경 모듈 뒤의 최종 속도로 검사해야 하므로 모듈 스택 맨 아래에 둔다
UCLASS()
class UParticleModuleCollision : public UParticleModule
{
	GENERATED_BODY()
public:
	UParticleModuleCollision();

	void Spawn(FParticleEmitterInstance& Owner, int32 Offset, float SpawnTime, FBaseParticle& Particle) override;
	void Update(FParticleEmitterInstance& Owner, int32 Offset, float DeltaTime) override;
	void BakeDistributions() override;

	uint32 RequiredBytes() const override { return sizeof(FParticleCollisionPayload); }

private:
	// 0 = 달라붙음, 1 = 같은 속도로 튐
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	FFloatDistribution Restitution;

	// 0 = 계속 미끄러짐, 1 = 표면 방향 속도를 모두 잃음
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, ClampMax=1, Delta=0.01))
	FFloatDistribution Friction;

	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution RotationDamping;

	// 생성 후 이 시간(초) 동안은 충돌하지 않는다 (생성 위치가 물체 안일 때)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	FFloatDistribution DelayAmount;

	// 0이면 제한 없음
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	int32 MaxCollisions = 3;

	UPROPERTY(EditAnywhere)
	EParticleCollisionComplete CompletionOption = EParticleCollisionComplete::Kill;

	// 충돌 반지름 = max(Size.X, Size.Y) × RadiusScale. Size는 지름이라 0.5면 스프라이트 크기
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float RadiusScale = 0.5f;

	// 튕긴 뒤 법선 방향 속도가 이보다 작으면 0으로 (바닥에서 떨지 않고 미끄러지거나 멈춤)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0))
	float MinBounceSpeed = 20.0f;

	// 충돌 지점에서 법선 방향으로 띄우는 거리 (다음 프레임에 시작부터 겹치지 않게)
	UPROPERTY(EditAnywhere, meta=(ClampMin=0, Delta=0.01))
	float SurfaceOffset = 0.1f;

	// 이펙트를 낸 액터와는 충돌하지 않는다 (총구 화염과 총)
	UPROPERTY(EditAnywhere)
	bool bIgnoreSourceActor = true;

	// Update 안에서만 쓰는 임시 버퍼. 매 프레임 할당하지 않으려고 재사용한다 (파티클 상태 아님)
	TArray<FPrimitiveSceneProxy*> CandidateCache;
};
