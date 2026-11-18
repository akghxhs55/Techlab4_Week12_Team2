#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "Containers/Map.h"
#include "ParticleModule.h"
#include "ParticleEmitter.generated.h"

class UParticleLODLevel;
class UParticleSystemComponent;
struct FParticleEmitterInstance;

UCLASS()
class UParticleEmitter : public UObject
{
	GENERATED_BODY()
public:
	virtual ~UParticleEmitter() override;

	// 모듈 구성이 바뀐 뒤 한 번 호출
	void Build();

	// 호출자가 소유하고 delete한다
	virtual FParticleEmitterInstance* CreateInstance(UParticleSystemComponent* InComponent);

	const TArray<UParticleLODLevel*>& GetLODLevels() const { return LODLevels; }
	UParticleLODLevel* GetLODLevel(int32 Index) const { return LODLevels.IsValidIndex(Index) ? LODLevels[Index] : nullptr; }
	int32 GetNumLODLevels() const { return LODLevels.Num(); }
	void AddLODLevel(UParticleLODLevel* InLODLevel);
	// SourceIndex 레벨을 모듈까지 복사해 InsertIndex 위치에 넣는다. Build()는 호출자 책임
	UParticleLODLevel* DuplicateLODLevel(int32 SourceIndex, int32 InsertIndex);
	// 레벨을 빼고 해제한다. 마지막 하나는 지우지 않는다. Build()는 호출자 책임
	bool RemoveLODLevel(int32 Index);

	int32 GetParticleSize() const { return ParticleSize; }
	int32 GetModuleOffset(UParticleModule* Module) const;   // Payload가 없는 모듈이면 0

	using Super::Serialize; // FArchive& 오버로드가 가려지지 않도록
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

private:
	// 파티클 한 칸의 크기와 모듈별 Payload 오프셋을 계산
	void CacheEmitterModuleInfo();
	// LOD 1 이상이 LOD 0과 같은 구조인지 검사하고 어긋나면 경고
	void ValidateLODLevels() const;
	// 배열 인덱스와 각 레벨의 Level 값을 맞춘다
	void RenumberLODLevels();

	int32 ParticleSize = 0;
	TArray<UParticleLODLevel*> LODLevels;
	TMap<UParticleModule*, int32> ModuleOffsetMap;
};