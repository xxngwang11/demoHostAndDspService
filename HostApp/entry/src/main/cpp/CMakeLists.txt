cmake_minimum_required(VERSION 3.4.1)
project(HostAppNative)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

add_definitions(-DOHOS_PLATFORM)

# Include the shared header as well as local sources
include_directories(
    ${NATIVERENDER_ROOT_PATH}
    ${NATIVERENDER_ROOT_PATH}/../../../../../shared
)

add_library(hostapp SHARED
    napi_init.cpp
    audio_native.cpp
)

target_link_libraries(hostapp PUBLIC
    libace_napi.z.so
    libhilog_ndk.z.so
)
