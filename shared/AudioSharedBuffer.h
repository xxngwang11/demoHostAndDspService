/**
 * AudioSharedBuffer.h
 *
 * Shared memory layout between HostApp and DspService:
 *
 *   [ AudioSharedHeader (128 bytes) ]
 *   [ Input  float32 PCM  (frames * channels * 4 bytes) ]
 *   [ Output float32 PCM  (frames * channels * 4 bytes) ]
 *
 * Total size = AUDIO_SHM_HEADER_SIZE + 2 * frames * channels * sizeof(float)
 *
 * Field offsets (packed, no alignment padding):
 *   0  magic              uint32  4
 *   4  version            uint32  4
 *   8  sampleRate         uint32  4
 *  12  channels           uint32  4
 *  16  frames             uint32  4
 *  20  format             uint32  4
 *  24  inputOffset        uint32  4
 *  28  outputOffset       uint32  4
 *  32  status             int32   4
 *  36  processingTimeNs   int64   8
 *  44  gain               float   4
 *  48  bypass             uint32  4
 *  52  _pad               uint8[76] 76
 * 128  (end of header)
 */

#pragma once

#include <stdint.h>

/* Magic 0x41534844: bytes 0x41='A' 0x53='S' 0x48='H' 0x44='D' (big-endian read) */
#define AUDIO_SHM_MAGIC    0x41534844u
#define AUDIO_SHM_VERSION  1u
#define AUDIO_SHM_HEADER_SIZE 128u

/* PCM format */
#define AUDIO_FORMAT_FLOAT32 0u

/* Status codes written by DspService into header.status */
#define AUDIO_STATUS_IDLE        0
#define AUDIO_STATUS_PROCESSING  1
#define AUDIO_STATUS_DONE        2
#define AUDIO_STATUS_ERROR      -1

/* Byte offsets used by ArkTS DataView (must match struct layout below) */
#define AUDIO_HDR_OFFSET_STATUS          32
#define AUDIO_HDR_OFFSET_PROC_TIME_NS    36
#define AUDIO_HDR_OFFSET_GAIN            44
#define AUDIO_HDR_OFFSET_BYPASS          48

#pragma pack(push, 1)
typedef struct AudioSharedHeader {
    uint32_t magic;              /* AUDIO_SHM_MAGIC                       */
    uint32_t version;            /* AUDIO_SHM_VERSION                     */
    uint32_t sampleRate;         /* e.g. 44100                            */
    uint32_t channels;           /* e.g. 2                                */
    uint32_t frames;             /* number of audio frames                */
    uint32_t format;             /* AUDIO_FORMAT_FLOAT32                  */
    uint32_t inputOffset;        /* byte offset of input PCM from buf[0]  */
    uint32_t outputOffset;       /* byte offset of output PCM from buf[0] */
    int32_t  status;             /* AUDIO_STATUS_*  (set by DspService)   */
    int64_t  processingTimeNs;   /* nanoseconds     (set by DspService)   */
    float    gain;               /* applied gain, 0.0 ~ 2.0               */
    uint32_t bypass;             /* 0 = process,  1 = bypass              */
    uint8_t  _pad[76];           /* pad to AUDIO_SHM_HEADER_SIZE = 128    */
} AudioSharedHeader;
#pragma pack(pop)

/* Total Ashmem size for a given stream */
static inline uint32_t audioShmTotalSize(uint32_t frames, uint32_t channels)
{
    return AUDIO_SHM_HEADER_SIZE + 2u * frames * channels * (uint32_t)sizeof(float);
}
