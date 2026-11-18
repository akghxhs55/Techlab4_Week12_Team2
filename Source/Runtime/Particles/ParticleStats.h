#pragma once

#include "Core/Stats/LightweightStats.h"

inline double SmoothParticleStatMs(double Previous, uint64 Cycles)
{
	constexpr double Alpha = 0.1;
	const double Ms = FPlatformTime::ToMilliseconds64(Cycles);
	return Previous + (Ms - Previous) * Alpha;
}
