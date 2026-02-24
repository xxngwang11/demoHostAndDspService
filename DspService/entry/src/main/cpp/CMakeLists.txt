cmake_minimum_required(VERSION 3.4.1)
project(DspServiceNative)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

add_definitions(-DOHOS_PLATFORM)

# Include the shared header as well as local sources
include_directories(
    ${NATIVERENDER_ROOT_PATH}
    ${NATIVERENDER_ROOT_PATH}/../../../../../shared
)

add_library(dspservice SHARED
    dsp_napi.cpp
    dsp_processor.cpp
)

target_link_libraries(dspservice PUBLIC
    libace_napi.z.so
    libhilog_ndk.z.so
)
