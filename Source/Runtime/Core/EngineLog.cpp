#include "EnginePCH.h"
#include "EngineLog.h"
#include "Core/Misc/Paths.h"
#include <fstream>
#include <chrono>
#include <mutex>

namespace
{
    TArray<ILogSink*>& GetSinks()
    {
        static TArray<ILogSink*> Sinks;
        return Sinks;
    }
}

void FLog::AddSink(ILogSink* Sink)
{
    if (Sink)
        GetSinks().Add(Sink);
}

void FLog::RemoveSink(ILogSink* Sink)
{
    TArray<ILogSink*>& Sinks = GetSinks();
    for (int32 Index = Sinks.Num() - 1; Index >= 0; --Index)
        if (Sinks[Index] == Sink)
            Sinks.RemoveAt(Index, 1);
}

void FLog::Emit(ELogVerbosity Verbosity, const FString& Message)
{
    // Session output survives clearing or closing the Output Log panel.
    {
        static std::mutex FileMutex;
        std::lock_guard<std::mutex> Lock(FileMutex);
        static std::ofstream File;
        static std::filesystem::path Directory;
        const auto WantedDirectory = (FPaths::HasProject() ? FPaths::ProjectDir() : FPaths::EngineDir()) / "Logs";
        if (Directory != WantedDirectory)
        {
            Directory = WantedDirectory;
            File.close(); File.clear();
            std::error_code Error;
            std::filesystem::create_directories(Directory, Error);
            if (!Error)
            {
                const auto Stamp = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                File.open(Directory / ("Hitori-" + std::to_string(Stamp) + ".log"), std::ios::out | std::ios::app);
            }
        }
        if (File)
        {
            const char* Labels[] = { "Input", "Info", "Warning", "Error" };
            File << '[' << Labels[static_cast<int>(Verbosity)] << "] " << Message;
            if (Message.empty() || Message.back() != '\n') File << '\n';
            File.flush();
        }
    }
    for (ILogSink* Sink : GetSinks())
        Sink->OnLog(Verbosity, Message);
}