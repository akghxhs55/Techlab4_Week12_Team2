#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "ParticleSystem.generated.h"

class UParticleEmitter;

UCLASS()
class UParticleSystem : public UObject
{
	GENERATED_BODY()
public:
	// 모든 에미터의 Build() 호출
	void BuildEmitters();
	virtual ~UParticleSystem() override;

	// 배열 순서 = 그리는 순서
	const TArray<UParticleEmitter*>& GetEmitters() const { return Emitters; }
	UParticleEmitter* GetEmitter(int32 Index) const { return Emitters.IsValidIndex(Index) ? Emitters[Index] : nullptr; }
	int32 GetNumEmitters() const { return Emitters.Num(); }

	void AddEmitter(UParticleEmitter* InEmitter);
	void RemoveEmitter(UParticleEmitter* InEmitter);

	void SetAssetPath(const FString& Path) { AssetPath = Path; }
	const FString& GetAssetPath() const { return AssetPath; }

	using Super::Serialize; // FArchive& 오버로드가 가려지지 않도록
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

private:
	TArray<UParticleEmitter*> Emitters;

	FString AssetPath;
};