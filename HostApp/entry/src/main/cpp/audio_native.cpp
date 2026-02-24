/**
 * audio_native.cpp — HostApp C++ helper implementations
 *
 * Provides:
 *   - Sine-wave PCM generation (float32, interleaved)
 *   - AudioSharedHeader serialisation
 *   - PCM-16 WAV file writer
 */

#include "audio_native.h"
#include "AudioSharedBuffer.h"

#include <cmath>
#include <cstring>
#include <fstream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace HostAudio {

/* ------------------------------------------------------------------ */
/*  generateSineWave                                                    */
/* ------------------------------------------------------------------ */
std::vector<uint8_t> generateSineWave(int sampleRate, int frames,
                                      int channels, float freqHz)
{
    const int totalSamples = frames * channels;
    std::vector<float> pcm(totalSamples);

    for (int i = 0; i < frames; ++i) {
        float s = std::sin(2.0f * static_cast<float>(M_PI) * freqHz
                           * static_cast<float>(i) / static_cast<float>(sampleRate));
        for (int ch = 0; ch < channels; ++ch) {
            pcm[i * channels + ch] = s;
        }
    }

    std::vector<uint8_t> out(totalSamples * sizeof(float));
    std::memcpy(out.data(), pcm.data(), out.size());
    return out;
}

/* ------------------------------------------------------------------ */
/*  buildHeader                                                         */
/* ------------------------------------------------------------------ */
std::vector<uint8_t> buildHeader(int sampleRate, int channels, int frames,
                                 float gain, int bypass)
{
    AudioSharedHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));

    hdr.magic        = AUDIO_SHM_MAGIC;
    hdr.version      = AUDIO_SHM_VERSION;
    hdr.sampleRate   = static_cast<uint32_t>(sampleRate);
    hdr.channels     = static_cast<uint32_t>(channels);
    hdr.frames       = static_cast<uint32_t>(frames);
    hdr.format       = AUDIO_FORMAT_FLOAT32;
    hdr.inputOffset  = AUDIO_SHM_HEADER_SIZE;
    hdr.outputOffset = AUDIO_SHM_HEADER_SIZE
                       + static_cast<uint32_t>(frames)
                       * static_cast<uint32_t>(channels)
                       * static_cast<uint32_t>(sizeof(float));
    hdr.status       = AUDIO_STATUS_IDLE;
    hdr.processingTimeNs = 0;
    hdr.gain         = gain;
    hdr.bypass       = static_cast<uint32_t>(bypass);

    std::vector<uint8_t> out(sizeof(AudioSharedHeader));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    return out;
}

/* ------------------------------------------------------------------ */
/*  WAV file writer                                                     */
/* ------------------------------------------------------------------ */

#pragma pack(push, 1)
struct WavHeader {
    char     riffId[4];      /* "RIFF"                              */
    uint32_t riffSize;       /* file size − 8                       */
    char     waveId[4];      /* "WAVE"                              */
    char     fmtId[4];       /* "fmt "                              */
    uint32_t fmtSize;        /* 16 for PCM                          */
    uint16_t audioFormat;    /* 1 = PCM                             */
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;       /* sampleRate * numChannels * bitDepth/8 */
    uint16_t blockAlign;     /* numChannels * bitDepth/8            */
    uint16_t bitsPerSample;  /* 16                                  */
    char     dataId[4];      /* "data"                              */
    uint32_t dataSize;       /* PCM-16 data size in bytes           */
};
#pragma pack(pop)

bool writeWavFile(const std::string& path, const void* pcmFloat32,
                  int byteCount, int sampleRate, int channels, int frames)
{
    if (!pcmFloat32 || byteCount <= 0) {
        return false;
    }

    const int numSamples = frames * channels;
    const float* src = static_cast<const float*>(pcmFloat32);

    /* Convert float32 → int16 with clamping */
    std::vector<int16_t> pcm16(numSamples);
    for (int i = 0; i < numSamples; ++i) {
        float s = src[i];
        if (s >  1.0f) s =  1.0f;
        if (s < -1.0f) s = -1.0f;
        pcm16[i] = static_cast<int16_t>(s * 32767.0f);
    }

    const uint32_t dataSize = static_cast<uint32_t>(numSamples) * sizeof(int16_t);

    WavHeader hdr;
    std::memcpy(hdr.riffId, "RIFF", 4);
    std::memcpy(hdr.waveId, "WAVE", 4);
    std::memcpy(hdr.fmtId,  "fmt ", 4);
    std::memcpy(hdr.dataId, "data", 4);
    hdr.riffSize      = 36u + dataSize;
    hdr.fmtSize       = 16u;
    hdr.audioFormat   = 1u;  /* PCM */
    hdr.numChannels   = static_cast<uint16_t>(channels);
    hdr.sampleRate    = static_cast<uint32_t>(sampleRate);
    hdr.bitsPerSample = 16u;
    hdr.byteRate      = static_cast<uint32_t>(sampleRate) * channels * 2u;
    hdr.blockAlign    = static_cast<uint16_t>(channels * 2);
    hdr.dataSize      = dataSize;

    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(&hdr),       sizeof(WavHeader));
    ofs.write(reinterpret_cast<const char*>(pcm16.data()), dataSize);

    return ofs.good();
}

} // namespace HostAudio
