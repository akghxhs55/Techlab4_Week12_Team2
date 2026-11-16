#pragma once

#include "ParticleTypes.h"
#include "ParticleEmitterInstance.h"

namespace ParticleHelper
{
	// 살아 있는 파티클 순회
	//  - Particle     : 공통 데이터
	//  - ParticleBase : 이 파티클 칸의 시작 주소
	template <typename FuncType>
	inline void ForEachParticle(FParticleEmitterInstance& Owner, FuncType&& Func)
	{
		uint8* const        ParticleData = Owner.ParticleData;
		const uint16* const ParticleIndices = Owner.ParticleIndices;
		const int32         Stride = Owner.ParticleStride;

		for (int32 i = Owner.ActiveParticles - 1; i >= 0; --i)
		{
			uint8* ParticleBase = ParticleData + ParticleIndices[i] * Stride;
			FBaseParticle& Particle = *reinterpret_cast<FBaseParticle*>(ParticleBase);

			if (Particle.Flags & STATE_Particle_Freeze)
				continue;

			Func(Particle, ParticleBase);
		}
	}

	// Payload 접근
	// Offset = 이 모듈의 Payload 시작 위치
	template <typename T>
	inline T& GetPayload(uint8* ParticleBase, int32 Offset)
	{
		assert(Offset >= static_cast<int32>(sizeof(FBaseParticle)) && "Payload가 없는 모듈에서 GetPayload 호출");
		return *reinterpret_cast<T*>(ParticleBase + Offset);
	}

	// Spawn에서는 FBaseParticle&만 받으므로 이 오버로드를 쓴다
	template <typename T>
	inline T& GetPayload(FBaseParticle& Particle, int32 Offset)
	{
		return GetPayload<T>(reinterpret_cast<uint8*>(&Particle), Offset);
	}

	// 렌더처럼 읽기만 하는 쪽용
	template <typename T>
	inline const T& GetPayload(const uint8* ParticleBase, int32 Offset)
	{
		assert(Offset >= static_cast<int32>(sizeof(FBaseParticle)) && "Payload가 없는 모듈에서 GetPayload 호출");
		return *reinterpret_cast<const T*>(ParticleBase + Offset);
	}

	template <typename T>
	inline const T& GetPayload(const FBaseParticle& Particle, int32 Offset)
	{
		return GetPayload<T>(reinterpret_cast<const uint8*>(&Particle), Offset);
	}
}