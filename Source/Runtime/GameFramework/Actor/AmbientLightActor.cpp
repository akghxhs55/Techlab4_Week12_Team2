#include "EnginePCH.h"
#include "AmbientLightActor.h"

AAmbientLightActor::AAmbientLightActor()
{
    AmbientLightComponent = CreateDefaultSubobject<UAmbientLightComponent>("USkyLightComponent");
	SetupLightRoot(AmbientLightComponent, "Engine/Icons/Sky.png");
}