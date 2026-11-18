#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "ParticleSystem.generated.h"

class UParticleEmitter;

UENUM()
enum class EParticleSystemLODMethod : uint8
{
	Automatic,          // 일정 간격으로 거리를 재서 고른다
	DirectSet,          // 게임 코드가 직접 정한다
	ActivateAutomatic,  // 활성화할 때 한 번만 거리로 고른다
};

UCLASS()
class UParticleSystem : public UObject
{
	GENERATED_BODY()
public:
	// LOD 설정을 에미터 LOD 수에 맞춘 뒤 모든 에미터의 Build() 호출
	void BuildEmitters();

	// LOD 수 = LODDistances 항목 수. 모든 에미터가 같은 수의 LOD 레벨을 갖는다
	int32 GetNumLODs() const { return LODDistances.Num(); }
	const TArray<float>& GetLODDistances() const { return LODDistances; }
	float GetLODDistance(int32 Index) const { return LODDistances.IsValidIndex(Index) ? LODDistances[Index] : 0.0f; }
	// LOD 0은 항상 0이라 바꿀 수 없다
	void SetLODDistance(int32 Index, float Distance);

	EParticleSystemLODMethod GetLODMethod() const { return LODMethod; }
	void SetLODMethod(EParticleSystemLODMethod InMethod) { LODMethod = InMethod; }
	float GetLODDistanceCheckTime() const { return LODDistanceCheckTime; }
	void SetLODDistanceCheckTime(float InSeconds) { LODDistanceCheckTime = InSeconds; }

	// Index - 1 레벨을 복사해 Index 위치에 넣는다 (1 <= Index <= GetNumLODs()). 모든 에미터에 적용
	bool InsertLOD(int32 Index);
	// LOD 0은 지울 수 없다 (1 <= Index < GetNumLODs()). 모든 에미터에 적용
	bool RemoveLOD(int32 Index);
	virtual ~UParticleSystem() override;

	// 배열 순서 = 그리는 순서
	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }
	UParticleEmitter* GetEmitter(int32 Index) const { return Emitters.IsValidIndex(Index) ? Emitters[Index] : nullptr; }
	int32 GetNumEmitters() const { return Emitters.Num(); }
	int32 FindEmitterIndex(const UParticleEmitter* InEmitter) const;

	void AddEmitter(UParticleEmitter* InEmitter);
	bool InsertEmitter(UParticleEmitter* InEmitter, int32 Index);
	// 배열에서 빼서 돌려준다(없으면 nullptr). 해제는 호출자 책임
	UParticleEmitter* RemoveEmitter(UParticleEmitter* InEmitter);
	bool MoveEmitter(int32 From, int32 To);
	void ClearEmitters();

	// 에디터용 복사본
	UParticleSystem* Duplicate(FString& OutError);

	bool CopyContentsFrom(UParticleSystem& Source, FString& OutError);

	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	const FString& GetAssetPath() const { return AssetPath; }

	using Super::Serialize; // FArchive& 오버로드가 가려지지 않도록
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

private:
	// LODDistances를 에미터 LOD 수에 맞추고 어긋난 설정을 경고
	void FixupLODSettings();

	TArray<UParticleEmitter*> Emitters;

	// LOD i는 카메라 거리가 LODDistances[i] 이상일 때 쓴다. [0]은 항상 0
	UPROPERTY(EditAnywhere)
	TArray<float> LODDistances;

	UPROPERTY(EditAnywhere)
	EParticleSystemLODMethod LODMethod = EParticleSystemLODMethod::Automatic;

	// Automatic일 때 거리 검사 간격 (초)
	UPROPERTY(EditAnywhere)
	float LODDistanceCheckTime = 0.25f;

	FString AssetPath;
};
