/**
 * dsp_napi.cpp — N-API bridge for DspService native library (libdspservice.so)
 *
 * Exported JavaScript API:
 *
 *   processAudio(inputBuffer: ArrayBuffer, gain: number, bypass: number)
 *       : { outputBuffer: ArrayBuffer; processingTimeNs: number }
 *
 *       inputBuffer  — float32 interleaved PCM (raw bytes)
 *       gain         — linear gain (0.0 ~ 2.0)
 *       bypass       — 0 = process, 1 = bypass
 *       outputBuffer — processed float32 PCM (same size as input)
 *       processingTimeNs — wall-clock DSP time in nanoseconds
 */

#include "napi/native_api.h"
#include "dsp_processor.h"
#include <hilog/log.h>
#include <cstring>

#define LOG_DOMAIN 0x0000
#define LOG_TAG    "DspServiceNative"
#define LOGI(fmt, ...) OH_LOG_INFO(LOG_APP, LOG_TAG ": " fmt, ##__VA_ARGS__)
#define LOGE(fmt, ...) OH_LOG_ERROR(LOG_APP, LOG_TAG ": " fmt, ##__VA_ARGS__)

/* ------------------------------------------------------------------ */
/*  processAudio                                                        */
/* ------------------------------------------------------------------ */
static napi_value ProcessAudio(napi_env env, napi_callback_info info)
{
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    /* arg0: inputBuffer (ArrayBuffer) */
    void*  bufData = nullptr;
    size_t bufLen  = 0;
    napi_get_arraybuffer_info(env, args[0], &bufData, &bufLen);

    /* arg1: gain (number) */
    double gain = 1.0;
    napi_get_value_double(env, args[1], &gain);

    /* arg2: bypass (number, 0 or 1) */
    int32_t bypass = 0;
    napi_get_value_int32(env, args[2], &bypass);

    const int numSamples = static_cast<int>(bufLen / sizeof(float));
    LOGI("processAudio samples=%d gain=%.3f bypass=%d", numSamples, (float)gain, bypass);

    auto res = DspProcessor::processAudio(bufData, numSamples,
                                          static_cast<float>(gain), bypass != 0);

    /* Create output ArrayBuffer */
    napi_value outputAb;
    void* outData = nullptr;
    napi_create_arraybuffer(env, res.outputBytes.size(), &outData, &outputAb);
    if (outData && !res.outputBytes.empty()) {
        std::memcpy(outData, res.outputBytes.data(), res.outputBytes.size());
    }

    /* Build result object: { outputBuffer, processingTimeNs } */
    napi_value obj;
    napi_create_object(env, &obj);

    napi_value keyBuf, keyTime;
    napi_create_string_utf8(env, "outputBuffer",     NAPI_AUTO_LENGTH, &keyBuf);
    napi_create_string_utf8(env, "processingTimeNs", NAPI_AUTO_LENGTH, &keyTime);

    napi_value valTime;
    napi_create_double(env, static_cast<double>(res.processingTimeNs), &valTime);

    napi_set_property(env, obj, keyBuf,  outputAb);
    napi_set_property(env, obj, keyTime, valTime);

    return obj;
}

/* ------------------------------------------------------------------ */
/*  Module registration                                                 */
/* ------------------------------------------------------------------ */
EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor desc[] = {
        { "processAudio", nullptr, ProcessAudio,
          nullptr, nullptr, nullptr, napi_default, nullptr },
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    return exports;
}
EXTERN_C_END

static napi_module dspServiceModule = {
    .nm_version       = 1,
    .nm_flags         = 0,
    .nm_filename      = nullptr,
    .nm_register_func = Init,
    .nm_modname       = "dspservice",
    .nm_priv          = nullptr,
    .reserved         = { nullptr },
};

extern "C" __attribute__((constructor)) void RegisterDspServiceModule(void)
{
    napi_module_register(&dspServiceModule);
}
