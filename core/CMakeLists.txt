add_library(vox_core INTERFACE)
add_library(vox::core ALIAS vox_core)
target_include_directories(vox_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(vox_core INTERFACE cxx_std_20)

if(VOX_BUILD_TESTS)
    add_executable(vox_core_tests
        tests/result_tests.cpp
        tests/spsc_queue_tests.cpp
        tests/ring_buffer_tests.cpp
        tests/object_channel_tests.cpp)
    target_link_libraries(vox_core_tests PRIVATE vox::core vox::testing Catch2::Catch2WithMain)
    vox_target_defaults(vox_core_tests)
    catch_discover_tests(vox_core_tests PROPERTIES LABELS core)
endif()
