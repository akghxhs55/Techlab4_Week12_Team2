#include "EnginePCH.h"
#include "ParticleModuleEventGenerator.h"

void UParticleModuleEventGenerator::BakeDistributions()
{
	// 이벤트마다 문자열을 FName으로 바꾸지 않도록 미리 만들어 둔다
	EventNames.Empty();
	for (const FParticleEventGeneratorInfo& Info : Events)
	{
		EventNames.Add(Info.CustomName.empty() ? FName() : FName(Info.CustomName));
	}
}

bool UParticleModuleEventGenerator::HasEvent(EParticleEventType Type) const
{
	for (const FParticleEventGeneratorInfo& Info : Events)
	{
		if (Info.Type == Type)
			return true;
	}
	return false;
}
