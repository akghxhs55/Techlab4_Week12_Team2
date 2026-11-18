#pragma once

#include "Core/Types.h"
#include "Core/NameTypes.h"
#include "Math/Vector.h"
#include "ParticleEventTypes.generated.h"

class UPrimitiveComponent;
class AActor;

UENUM()
enum class EParticleEventType : uint8
{
	Any,        // Receiver가 종류를 가리지 않을 때
	Spawn,      // 파티클 생성
	Death,      // 수명 종료
	Collision,  // 충돌
	Burst,      // Burst 발사 (에미터 단위)
};

// 모든 이벤트의 공통 부분. 파티클 칸은 swap 후 다른 파티클이 재사용하므로 값으로 복사해 둔다.
// 위치·속도·방향은 모두 월드 공간
struct FParticleEventData
{
	EParticleEventType Type = EParticleEventType::Any;
	FName   EventName;          // Generator가 붙인 이름. Receiver가 이 이름으로 골라 받는다
	int32   EmitterIndex = -1;  // 이벤트를 낸 에미터 (컴포넌트의 EmitterInstances 순서)
	float   EmitterTime = 0.0f;
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Direction = FVector::ZeroVector;
	int32   CustomData = 0;
};

struct FParticleEventSpawnData : FParticleEventData
{
};

struct FParticleEventDeathData : FParticleEventData
{
	float ParticleTime = 0.0f;  // 죽은 파티클의 나이 (초)
};

struct FParticleEventCollisionData : FParticleEventData
{
	FVector Normal = FVector::ZeroVector;
	float   Time = 0.0f;                      // 이번 프레임 경로상 충돌 위치 (0~1)
	float   ParticleTime = 0.0f;              // 충돌한 파티클의 나이 (초)
	UPrimitiveComponent* HitComponent = nullptr;  // 같은 프레임 안에서만 유효
	AActor* HitActor = nullptr;
};

struct FParticleEventBurstData : FParticleEventData
{
	int32 ParticleCount = 0;
};
