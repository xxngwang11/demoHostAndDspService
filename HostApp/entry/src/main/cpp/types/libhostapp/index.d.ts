/**
 * Type declarations for libhostapp.so (HostApp native library).
 * Used by the ArkTS compiler for type-checking.
 */

/**
 * Generate a stereo (or multi-channel) sine-wave PCM buffer.
 * @param sampleRate  e.g. 44100
 * @param frames      number of sample frames
 * @param channels    number of audio channels (typically 2)
 * @param freqHz      frequency of the sine wave in Hz (e.g. 440.0)
 * @returns ArrayBuffer containing interleaved float32 PCM samples
 */
export declare function generateSineWave(
  sampleRate: number,
  frames: number,
  channels: number,
  freqHz: number
): ArrayBuffer;

/**
 * Serialise an AudioSharedHeader into a 128-byte array.
 * @param sampleRate  stream sample rate
 * @param channels    number of channels
 * @param frames      number of frames
 * @param gain        DSP gain value (0.0 ~ 2.0)
 * @param bypass      0 = process,  1 = bypass
 * @returns number[] of length 128, each element is a byte (0-255)
 */
export declare function buildHeader(
  sampleRate: number,
  channels: number,
  frames: number,
  gain: number,
  bypass: number
): number[];

/**
 * Convert float32 PCM to PCM-16 and write a standard WAV file.
 * @param path       absolute destination path on the device
 * @param buffer     ArrayBuffer containing float32 interleaved PCM
 * @param sampleRate stream sample rate
 * @param channels   number of audio channels
 * @param frames     number of sample frames
 * @returns true on success, false on failure
 */
export declare function writeWavFile(
  path: string,
  buffer: ArrayBuffer,
  sampleRate: number,
  channels: number,
  frames: number
): boolean;
