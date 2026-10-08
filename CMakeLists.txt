cmake_minimum_required(VERSION 3.25)

project(Voxwright
    VERSION 0.1.0
    DESCRIPTION "Real-time voice changer and soundboard"
    HOMEPAGE_URL "https://github.com/Yousefz-z/Test"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(VOX_BUILD_APP "Build the Qt desktop application" ON)
option(VOX_BUILD_TESTS "Build the automated test suites" ON)
option(VOX_BUILD_TOOLS "Build internal development tools (benchmarks, sound pack renderer)" ON)
option(VOX_ENABLE_ML "Build the optional neural voice conversion track (needs ONNX Runtime)" OFF)
option(VOX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
set(VOX_SANITIZE "" CACHE STRING "Sanitizers to enable: empty, 'address;undefined', or 'thread'")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(VoxCompilerOptions)
include(VoxSanitizers)

if(VOX_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(core)
if(VOX_BUILD_TESTS)
    add_subdirectory(testing)
endif()
add_subdirectory(dsp)
add_subdirectory(plugins)
add_subdirectory(devices)
add_subdirectory(engine)

if(VOX_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(VOX_BUILD_APP)
    add_subdirectory(app)
endif()
