# Test-only support library: allocation trap, WAV I/O, synthetic test signals,
# measurement helpers, and the offline render path used to verify effects.
# Never linked into the application.

find_package(signalsmith-linear CONFIG REQUIRED)

add_library(vox_testing STATIC
    src/alloc_trap.cpp
    src/wav.cpp
    src/signals.cpp
    src/analysis.cpp)
add_library(vox::testing ALIAS vox_testing)
target_include_directories(vox_testing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vox_testing PUBLIC vox::core PRIVATE signalsmith::linear)
vox_target_defaults(vox_testing)
if(VOX_SANITIZE)
    # Instrumented code runs several times slower; see vox/testing/timing.hpp.
    target_compile_definitions(vox_testing PUBLIC VOX_TESTING_INSTRUMENTED)
endif()
if("thread" IN_LIST VOX_SANITIZE)
    # The ThreadSanitizer runtime defines operator new/delete itself.
    target_compile_definitions(vox_testing PRIVATE VOX_TESTING_NO_ALLOCATION_TRAP)
endif()
