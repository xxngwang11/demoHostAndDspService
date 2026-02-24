/**
 * dsp_processor.h — DSP processing engine declarations
 *
 * All operations work on float32 interleaved PCM samples.
 */

#pragma once

#include <cstdint>
#include <vector>

namespace DspProcessor {

/** Result returned by processAudio() */
struct ProcessResult {
    /** Output PCM as raw bytes (float32 interleaved) */
    std::vector<uint8_t> outputBytes;
    /** Wall-clock processing duration in nanoseconds */
    int64_t processingTimeNs;
};

/**
 * Process a block of float32 interleaved PCM.
 *
 * @param inputPcm   pointer to the input float32 PCM data
 * @param numSamples total sample count (frames × channels)
 * @param gain       linear gain applied before soft-clipping (ignored when bypass=true)
 * @param bypass     when true, output is a verbatim copy of the input
 * @return ProcessResult with output bytes and timing
 */
ProcessResult processAudio(const void* inputPcm, int numSamples,
                           float gain, bool bypass);

} // namespace DspProcessor
