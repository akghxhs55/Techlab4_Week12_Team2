#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"

#include <filesystem>

class UParticleSystem;

namespace ParticleSystemFile
{
    bool Read(const std::filesystem::path& Path, UParticleSystem& ParticleSystem, FString& OutError);
    bool Write(const std::filesystem::path& Path, UParticleSystem& ParticleSystem, FString& OutError);
    bool IsParticleSystemPath(const FString& Path);

    bool SaveToBytes(UParticleSystem& ParticleSystem, TArray<uint8>& OutBytes, FString& OutError);
    bool LoadFromBytes(const TArray<uint8>& Bytes, UParticleSystem& ParticleSystem, FString& OutError);
}
