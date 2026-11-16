#pragma once

#include "Core/EngineString.h"

#include <filesystem>

class UParticleSystem;

namespace ParticleSystemFile
{
    bool Read(const std::filesystem::path& Path, UParticleSystem& ParticleSystem, FString& OutError);
    bool Write(const std::filesystem::path& Path, UParticleSystem& ParticleSystem, FString& OutError);
    bool IsParticleSystemPath(const FString& Path);
}
