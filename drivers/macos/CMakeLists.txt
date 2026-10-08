# Voxwright Virtual Microphone, an AudioServerPlugIn for macOS. Built only
# with -DVOX_BUILD_DRIVER=ON on macOS. See README.md for what is and is not
# verified.
cmake_minimum_required(VERSION 3.24)
project(VoxwrightMic VERSION 0.1.0 LANGUAGES C)

if(NOT APPLE)
    message(FATAL_ERROR "The Voxwright virtual microphone driver builds only on macOS.")
endif()

add_library(VoxwrightMic MODULE voxwright_mic.c)
set_target_properties(VoxwrightMic PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION driver
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    C_STANDARD 17
    C_STANDARD_REQUIRED ON)
target_compile_options(VoxwrightMic PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(VoxwrightMic PRIVATE "-framework CoreAudio" "-framework CoreFoundation")
