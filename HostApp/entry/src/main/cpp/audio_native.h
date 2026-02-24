/**
 * audio_native.h — HostApp C++ native helper declarations
 *
 * Functions exposed to ArkTS via N-API (see napi_init.cpp):
 *   generateSineWave  → ArrayBuffer (float32 PCM)
 *   buildHeader       → number[]   (128 raw bytes of AudioSharedHeader)
 *   writeWavFile      → boolean
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HostAudio {

/**
 * Generate a stereo sine-wave PCM buffer (float32, interleaved).
 * @param sampleRate  e.g. 44100
 * @param frames      number of sample frames
 * @param channels    number of channels (1 or 2)
 * @param freqHz      sine frequency in Hz (e.g. 440.0)
 * @return raw bytes of the float32 PCM (size = frames * channels * 4)
 */
std::vector<uint8_t> generateSineWave(int sampleRate, int frames,
                                      int channels, float freqHz);

/**
 * Serialize an AudioSharedHeader into a 128-byte byte vector.
 * @param sampleRate  stream sample rate
 * @param channels    number of channels
 * @param frames      number of frames
 * @param gain        DSP gain parameter
 * @param bypass      0 = process, 1 = bypass
 * @return 128 bytes suitable for writing to offset 0 of the Ashmem
 */
std::vector<uint8_t> buildHeader(int sampleRate, int channels, int frames,
                                 float gain, int bypass);

/**
 * Write a PCM-16 WAV file from a float32 PCM buffer.
 * Samples are clamped to [-1, 1] and scaled to int16.
 * @param path       destination file path
 * @param pcmFloat32 pointer to float32 interleaved PCM data
 * @param byteCount  size of pcmFloat32 in bytes
 * @param sampleRate stream sample rate
 * @param channels   number of channels
 * @param frames     number of sample frames
 * @return true on success
 */
bool writeWavFile(const std::string& path, const void* pcmFloat32,
                  int byteCount, int sampleRate, int channels, int frames);

} // namespace HostAudio
