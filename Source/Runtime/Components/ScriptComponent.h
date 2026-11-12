#pragma once

#include "ActorComponent.h"
#include "ScriptComponent.generated.h"

struct FLuaScriptInstance;

// Lua 스크립트 하나를 액터에 붙인다. 액터에 여러 개를 달 수 있고, 스크립트마다 environment가 따로다.
// 이 헤더에서 sol.hpp를 include하지 않도록 인스턴스는 포인터로 둔다.
UCLASS()
class UScriptComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScriptComponent();
	virtual ~UScriptComponent() override;

	virtual void BeginPlay() override;
	virtual void EndPlay() override;
	virtual void TickComponent(float DeltaTime) override;

	const FString& GetScriptPath() const { return ScriptPath; }
	// 프로젝트 상대 경로로 정규화해서 저장한다. 플레이 중이면 스크립트를 바로 다시 시작한다.
	void SetScriptPath(const FString& InPath);

	// 함수가 없으면 무시한다.
	void CallScript(const char* Name, AActor* Other);

private:
	void StartScript();
	void StopScript();

	// "Content/Scripts/Foo.lua" 같은 프로젝트 상대 경로. 저장만 하고 프로퍼티 목록에는 내지 않는다.
	// 디테일 패널이 전용 입력란으로 SetScriptPath를 거쳐 바꾼다.
	UPROPERTY()
	FString ScriptPath;

	FLuaScriptInstance* Instance = nullptr;
};
