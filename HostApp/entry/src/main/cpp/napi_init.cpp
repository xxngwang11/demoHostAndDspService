/**
 * napi_init.cpp — N-API bridge for HostApp native library (libhostapp.so)
 *
 * Exported JavaScript API:
 *
 *   generateSineWave(sampleRate: number, frames: number,
 *                    channels: number, freqHz: number): ArrayBuffer
 *       Returns a float32 interleaved PCM buffer.
 *
 *   buildHeader(sampleRate: number, channels: number, frames: number,
 *               gain: number, bypass: number): number[]
 *       Returns 128 raw bytes of AudioSharedHeader for writing to Ashmem.
 *
 *   writeWavFile(path: string, buffer: ArrayBuffer,
 *                sampleRate: number, channels: number, frames: number): boolean
 *       Converts float32 PCM to PCM-16 and writes a WAV file.
 */

#include "napi/native_api.h"
#include "audio_native.h"
#include <hilog/log.h>
#include <cstring>

#define LOG_DOMAIN  0x0000
#define LOG_TAG     "HostAppNative"
#define LOGI(fmt, ...) OH_LOG_INFO(LOG_APP, LOG_TAG ": " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) OH_LOG_ERROR(LOG_APP, LOG_TAG ": " fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/*  generateSineWave                                                    */
/* ------------------------------------------------------------------ */
static napi_value GenerateSineWave(napi_env env, napi_callback_info info)
{
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int32_t sampleRate = 44100, frames = 44100, channels = 2;
    double  freqHz = 440.0;

    napi_get_value_int32(env, args[0], &sampleRate);
    napi_get_value_int32(env, args[1], &frames);
    napi_get_value_int32(env, args[2], &channels);
    napi_get_value_double(env, args[3], &freqHz);

    LOGI("generateSineWave sr=%d frm=%d ch=%d freq=%.1f", sampleRate, frames, channels, (float)freqHz);

    auto bytes = HostAudio::generateSineWave(sampleRate, frames, channels,
                                             static_cast<float>(freqHz));

    napi_value result;
    void* data = nullptr;
    napi_create_arraybuffer(env, bytes.size(), &data, &result);
    if (data && !bytes.empty()) {
        std::memcpy(data, bytes.data(), bytes.size());
    }
    return result;
}

/* ------------------------------------------------------------------ */
/*  buildHeader                                                         */
/* ------------------------------------------------------------------ */
static napi_value BuildHeader(napi_env env, napi_callback_info info)
{
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    int32_t sampleRate = 44100, channels = 2, frames = 44100, bypass = 0;
    double  gain = 0.5;

    napi_get_value_int32(env, args[0], &sampleRate);
    napi_get_value_int32(env, args[1], &channels);
    napi_get_value_int32(env, args[2], &frames);
    napi_get_value_double(env, args[3], &gain);
    napi_get_value_int32(env, args[4], &bypass);

    auto bytes = HostAudio::buildHeader(sampleRate, channels, frames,
                                        static_cast<float>(gain), bypass);

    /* Return as number[] (each element is a byte value 0-255) */
    napi_value arr;
    napi_create_array_with_length(env, bytes.size(), &arr);
    for (uint32_t i = 0; i < static_cast<uint32_t>(bytes.size()); ++i) {
        napi_value elem;
        napi_create_uint32(env, bytes[i], &elem);
        napi_set_element(env, arr, i, elem);
    }
    return arr;
}

/* ------------------------------------------------------------------ */
/*  writeWavFile                                                        */
/* ------------------------------------------------------------------ */
static napi_value WriteWavFile(napi_env env, napi_callback_info info)
{
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    /* arg0: file path (string) */
    size_t pathLen = 0;
    napi_get_value_string_utf8(env, args[0], nullptr, 0, &pathLen);
    std::string path(pathLen + 1, '\0');
    napi_get_value_string_utf8(env, args[0], &path[0], pathLen + 1, &pathLen);
    path.resize(pathLen);

    /* arg1: PCM ArrayBuffer */
    void*  bufData = nullptr;
    size_t bufLen  = 0;
    napi_get_arraybuffer_info(env, args[1], &bufData, &bufLen);

    /* arg2: sampleRate, arg3: channels, arg4: frames */
    int32_t sampleRate = 44100, channels = 2, frames = 44100;
    napi_get_value_int32(env, args[2], &sampleRate);
    napi_get_value_int32(env, args[3], &channels);
    napi_get_value_int32(env, args[4], &frames);

    LOGI("writeWavFile path=%s sr=%d ch=%d frm=%d bufLen=%zu",
         path.c_str(), sampleRate, channels, frames, bufLen);

    bool ok = HostAudio::writeWavFile(path, bufData, static_cast<int>(bufLen),
                                      sampleRate, channels, frames);

    napi_value result;
    napi_get_boolean(env, ok, &result);
    return result;
}

/* ------------------------------------------------------------------ */
/*  Module registration                                                 */
/* ------------------------------------------------------------------ */
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "generateSineWave", nullptr, GenerateSineWave, nullptr, nullptr, nullptr, napi_default, nullptr },
        { "buildHeader",      nullptr, BuildHeader,      nullptr, nullptr, nullptr, napi_default, nullptr },
        { "writeWavFile",     nullptr, WriteWavFile,     nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module hostAppModule = {
    .nm_version       = 1,
    .nm_flags         = 0,
    .nm_filename      = nullptr,
    .nm_register_func = Init,
    .nm_modname       = "hostapp",
    .nm_priv          = nullptr,
    .reserved         = { nullptr },
};

extern "C" __attribute__((constructor)) void RegisterHostAppModule(void)
{
    napi_module_register(&hostAppModule);
}
