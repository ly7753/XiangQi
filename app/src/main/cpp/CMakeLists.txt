cmake_minimum_required(VERSION 3.22.1)
project(nativecpp)

if(DEFINED CMAKE_ANDROID_NDK)
    set(NDK_PATH "${CMAKE_ANDROID_NDK}")
elseif(DEFINED ENV{ANDROID_NDK_HOME})
    set(NDK_PATH "$ENV{ANDROID_NDK_HOME}")
endif()

add_library(nativecpp SHARED
    native_main.cpp
    chess.cpp
    uci_engine.cpp
    render.cpp
    audio_engine.cpp
    egl_engine.cpp
)

target_compile_options(nativecpp PRIVATE -flto -O3 -ffast-math -fvisibility=hidden)
target_link_options(nativecpp PRIVATE -flto -O3 -Wl,-s)

target_link_libraries(nativecpp android aaudio EGL GLESv3 dl)
