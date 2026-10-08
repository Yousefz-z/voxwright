# Warning and code generation flags shared by every first-party target.
# Third-party code is consumed through imported targets and gets none of these.

add_library(vox_compiler_options INTERFACE)
add_library(vox::compiler_options ALIAS vox_compiler_options)

if(MSVC)
    # C4324 reports padding added by alignas. The lock-free queues align their
    # indices to cache lines on purpose, so the padding is the intent.
    target_compile_options(vox_compiler_options INTERFACE
        /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc /bigobj
        /wd4324
        $<$<BOOL:${VOX_WARNINGS_AS_ERRORS}>:/WX>)
    target_compile_definitions(vox_compiler_options INTERFACE
        NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(vox_compiler_options INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align
        -Wunused -Woverloaded-virtual -Wdouble-promotion
        -Wformat=2 -Wimplicit-fallthrough -Wconversion
        $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
        $<$<BOOL:${VOX_WARNINGS_AS_ERRORS}>:-Werror>)
    # GCC's -Wnull-dereference reports false positives on std::vector
    # indexing at -O2, so it is enabled for Clang only.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(vox_compiler_options INTERFACE -Wnull-dereference)
    endif()
endif()

# Real-time DSP must not hit denormal slowdowns; the engine also sets FTZ/DAZ
# at runtime, this only keeps the math library from being pessimized.
if(NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vox_compiler_options INTERFACE -fno-math-errno)
endif()

# Every first-party library and executable links this target.
function(vox_target_defaults target)
    target_link_libraries(${target} PRIVATE vox::compiler_options)
    vox_apply_sanitizers(${target})
endfunction()
