#pragma once

#include "Core/Types.h"
#include "UObject/Object.h"
#include "ParticleLODLevel.generated.h"

class UParticleModule;
class UParticleModuleRequired;
class UParticleModuleSpawn;
class UParticleModuleTypeDataBase;
class UParticleModuleEventGenerator;
class UParticleModuleEventReceiverBase;

UCLASS()
class UParticleLODLevel : public UObject
{
	GENERATED_BODY()
public:
	virtual ~UParticleLODLevel() override;

	// Modules를 보고 SpawnModules, UpdateModules를 다시 만든다.
	void UpdateModuleLists();

	// 직렬화로 모듈까지 깊은 복사한다. 실패하면 nullptr. 호출자가 소유한다
	UParticleLODLevel* Duplicate(UObject* Outer);

	void SetLevel(int32 InLevel) { Level = InLevel; }
	int32 GetLevel() const { return Level; }
	bool  IsEnabled() const { return bEnabled; }
	void  SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

	UParticleModuleRequired* GetRequiredModule() const { return RequiredModule; }
	void SetRequiredModule(UParticleModuleRequired* InModule) { RequiredModule = InModule; }

	// Modules 배열에는 넣지 않는다
	UParticleModuleSpawn* GetSpawnModule() const { return SpawnModule; }
	void SetSpawnModule(UParticleModuleSpawn* InModule) { SpawnModule = InModule; }
	UParticleModuleTypeDataBase* GetTypeDataModule() const { return TypeDataModule; }
	void SetTypeDataModule(UParticleModuleTypeDataBase* InModule) { TypeDataModule = InModule; }

	const TArray<UParticleModule*>& GetModules() const { return Modules; }
	int32 FindModuleIndex(const UParticleModule* InModule) const;
	void AddModule(UParticleModule* InModule);
	bool InsertModule(UParticleModule* InModule, int32 Index);
	// 해제는 호출자 책임. 없으면 false
	bool RemoveModule(const UParticleModule* InModule);
	bool MoveModule(int32 From, int32 To);

	// UpdateModuleLists()만 채운다. 바깥에서는 읽기만 한다.
	const TArray<UParticleModule*>& GetSpawnModules() const { return SpawnModules; }
	const TArray<UParticleModule*>& GetUpdateModules() const { return UpdateModules; }
	// Modules 중 첫 EventGenerator. 없으면 nullptr. 꺼져 있어도 돌려주므로 호출자가 IsEnabled를 본다
	UParticleModuleEventGenerator* GetEventGenerator() const { return EventGenerator; }
	// Modules 중 이벤트 Receiver들 (꺼진 것 포함)
	const TArray<UParticleModuleEventReceiverBase*>& GetEventReceivers() const { return EventReceivers; }

	using Super::Serialize; // FArchive& 오버로드가 가려지지 않도록
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

private:
	int32 Level = 0;

	UPROPERTY(EditAnywhere)
	bool  bEnabled = true;

	UParticleModuleRequired* RequiredModule = nullptr;
	UParticleModuleSpawn*    SpawnModule = nullptr;
	UParticleModuleTypeDataBase* TypeDataModule = nullptr;
	TArray<UParticleModule*> Modules;
	TArray<UParticleModule*> SpawnModules;
	TArray<UParticleModule*> UpdateModules;
	UParticleModuleEventGenerator* EventGenerator = nullptr;
	TArray<UParticleModuleEventReceiverBase*> EventReceivers;
};