#include "EnginePCH.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"

#include "../Components/ActorComponent.h"
#include "Asset/AssetManager.h"

// 카메라 컴포넌트를 기본 서브오브젝트로 만들어 루트에 연결하고, 에디터 아이콘 빌보드를 붙인다.
ACameraActor::ACameraActor()
{
	CameraComponent = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	SetRootComponent(CameraComponent);

	// 월드의 메인 카메라도 이 클래스지만 RegisterAllComponents를 거치지 않아 씬에 그려지지 않는다.
	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>("UBillboardComponent");
	BillboardComponent->SetEditorIcon(true);
	BillboardComponent->SetIconSize(2.0f);
	BillboardComponent->SetHiddenInGame(true);
	BillboardComponent->SetupAttachment(CameraComponent);
	BillboardComponent->SetHiddenInDetails(true);

	UMaterial* Base = UAssetManager::GetAssetByPath<UMaterial>("BillboardMaterial");
	UTexture2D* Icon = UAssetManager::Get().LoadTexture("Engine/Icons/Camera.png");
	if (Base && Icon)
	{
		UMaterial* IconMat = UMaterial::CreateInstance(Base);
		IconMat->Textures[0] = Icon;
		BillboardComponent->SetMaterial(0, IconMat);
	}
}
