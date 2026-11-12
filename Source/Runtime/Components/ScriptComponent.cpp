#include "EnginePCH.h"
#include "ScriptComponent.h"

#include "GameFramework/Actor.h"
#include "Script/LuaScriptInstance.h"
#include "Script/LuaScriptManager.h"

UScriptComponent::UScriptComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UScriptComponent::~UScriptComponent()
{
	// EndPlay 없이 지워지는 경로에서도 인스턴스는 놓는다. Lua EndPlay는 부르지 않는다.
	delete Instance;
}

void UScriptComponent::BeginPlay()
{
	Super::BeginPlay();
	StartScript();
}

void UScriptComponent::EndPlay()
{
	StopScript();
	Super::EndPlay();
}

void UScriptComponent::TickComponent(float DeltaTime)
{
	Super::TickComponent(DeltaTime);
	if (Instance)
		Instance->Tick(DeltaTime);
}

void UScriptComponent::SetScriptPath(const FString& InPath)
{
	const FString NewPath = FLuaScriptManager::NormalizeScriptPath(InPath);
	if (NewPath == ScriptPath)
		return;

	const bool bRestart = HasBegunPlay();
	if (bRestart) StopScript();
	ScriptPath = NewPath;
	if (bRestart) StartScript();
}

void UScriptComponent::CallScript(const char* Name, AActor* Other)
{
	if (Instance)
		Instance->Call(Name, Other);
}

void UScriptComponent::StartScript()
{
	if (Instance || ScriptPath.empty() || !GetOwner())
		return;

	// 로드에 실패해도 인스턴스는 남긴다. 파일을 고치면 핫 리로드가 다시 읽는다.
	Instance = new FLuaScriptInstance();
	if (!Instance->Load(GetOwner(), ScriptPath))
	{
		HTR_LOG(Warning, "[Lua] {}.{} : script is not running ({})", GetOwner()->GetName(), GetName(), ScriptPath);
		return;
	}
	Instance->Call("BeginPlay");
}

void UScriptComponent::StopScript()
{
	if (!Instance)
		return;

	Instance->Call("EndPlay");
	delete Instance;
	Instance = nullptr;
}
