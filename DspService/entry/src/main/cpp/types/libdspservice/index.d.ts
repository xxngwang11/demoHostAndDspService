/**
 * Type declarations for libdspservice.so (DspService native library).
 * Used by the ArkTS compiler for type-checking.
 */

/** Result object returned by processAudio() */
export interface DspProcessResult {
  /** Processed float32 interleaved PCM (same byte size as input) */
  outputBuffer: ArrayBuffer;
  /** Wall-clock DSP processing duration in nanoseconds */
  processingTimeNs: number;
}

/**
 * Process a block of float32 interleaved PCM.
 *
 * @param inputBuffer  ArrayBuffer of float32 PCM samples
 * @param gain         linear gain (0.0 ~ 2.0)
 * @param bypass       0 = apply gain+softclip, 1 = copy input unchanged
 * @returns DspProcessResult
 */
export declare function processAudio(
  inputBuffer: ArrayBuffer,
  gain: number,
  bypass: number
): DspProcessResult;
