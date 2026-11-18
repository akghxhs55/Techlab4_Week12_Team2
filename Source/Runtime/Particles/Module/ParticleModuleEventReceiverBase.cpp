#include "EnginePCH.h"
#include "ParticleModuleEventReceiverBase.h"

void UParticleModuleEventReceiverBase::BakeDistributions()
{
	// 이벤트마다 문자열을 비교하지 않도록 FName으로 바꿔 둔다
	CachedEventName = EventName.empty() ? FName() : FName(EventName);
}

bool UParticleModuleEventReceiverBase::WillProcessEvent(const FParticleEventData& Event) const
{
	if (EventGeneratorType != EParticleEventType::Any && EventGeneratorType != Event.Type)
		return false;
	return CachedEventName.IsNone() || CachedEventName == Event.EventName;
}
