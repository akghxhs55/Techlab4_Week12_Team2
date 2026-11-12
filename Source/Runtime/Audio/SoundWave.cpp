#include "EnginePCH.h"
#include "Audio/SoundWave.h"
#include "Audio/MiniaudioInclude.h"
#include <limits>

bool USoundWave::LoadFromFile(const FString& Path)
{
    ma_decoder_config Config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder Decoder;
    if (ma_decoder_init_file(Path.c_str(), &Config, &Decoder) != MA_SUCCESS)
    {
        HTR_LOG(Error, "[Audio] Cannot decode {}", Path);
        return false;
    }
    if (Decoder.outputChannels > 2)
    {
        ma_decoder_uninit(&Decoder);
        Config = ma_decoder_config_init(ma_format_f32, 2, 0);
        if (ma_decoder_init_file(Path.c_str(), &Config, &Decoder) != MA_SUCCESS) return false;
    }
    struct FDecoderGuard
    {
        ma_decoder& Decoder;
        ~FDecoderGuard() { ma_decoder_uninit(&Decoder); }
    } Guard{ Decoder };

    const uint32 Channels = Decoder.outputChannels;
    const uint32 Rate = Decoder.outputSampleRate;
    if (Channels == 0 || Rate == 0) return false;

    // Decode into temporary storage: retries replace audio, failed reloads retain the old clip.
    TArray<float> Decoded;
    constexpr ma_uint64 ChunkFrames = 16384;
    const int32 ChunkSamples = static_cast<int32>(ChunkFrames * Channels);
    while (true)
    {
        const int32 OldNum = Decoded.Num();
        if (OldNum > std::numeric_limits<int32>::max() - ChunkSamples) return false;
        Decoded.SetNum(OldNum + ChunkSamples);
        ma_uint64 Read = 0;
        const ma_result Result = ma_decoder_read_pcm_frames(&Decoder, Decoded.GetData() + OldNum, ChunkFrames, &Read);
        if (Result != MA_SUCCESS && Result != MA_AT_END) return false;
        Decoded.SetNum(OldNum + static_cast<int32>(Read * Channels));
        if (Read < ChunkFrames) break;
    }
    if (Decoded.IsEmpty()) return false;
    PCMData = std::move(Decoded);
    NumChannels = Channels;
    SampleRate = Rate;
    NumFrames = PCMData.Num() / Channels;
    Duration = static_cast<float>(NumFrames) / static_cast<float>(Rate);
    SourcePath = Path;
    return true;
}
