/**
 * dsp_processor.cpp — DSP processing engine implementation
 *
 * Supported modes:
 *   bypass = true  → output = input  (verbatim copy)
 *   bypass = false → output = tanh(input * gain)   (gain + soft clip)
 */

#include "dsp_processor.h"

#include <chrono>
#include <cmath>
#include <cstring>

namespace DspProcessor {

ProcessResult processAudio(const void* inputPcm, int numSamples,
                           float gain, bool bypass)
{
    ProcessResult result;
    result.outputBytes.resize(static_cast<size_t>(numSamples) * sizeof(float));
    result.processingTimeNs = 0;

    const float* src = static_cast<const float*>(inputPcm);
    float*       dst = reinterpret_cast<float*>(result.outputBytes.data());

    auto t0 = std::chrono::steady_clock::now();

    if (bypass) {
        /* Bypass: copy input to output unchanged */
        std::memcpy(dst, src, static_cast<size_t>(numSamples) * sizeof(float));
    } else {
        /* Apply gain then soft-clip via tanh to prevent overflow */
        for (int i = 0; i < numSamples; ++i) {
            dst[i] = std::tanh(src[i] * gain);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    result.processingTimeNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    return result;
}

} // namespace DspProcessor
