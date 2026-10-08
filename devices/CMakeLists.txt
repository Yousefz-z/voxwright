find_package(miniaudio CONFIG REQUIRED)
find_package(Threads REQUIRED)

# miniaudio and stb_vorbis are third-party C compiled in one translation unit,
# without the project's warning flags.
add_library(vox_miniaudio STATIC src/miniaudio_impl.c)
target_link_libraries(vox_miniaudio PUBLIC miniaudio::miniaudio Threads::Threads)
if(APPLE)
    # Link the frameworks directly instead of loading them at run time.
    target_compile_definitions(vox_miniaudio PRIVATE MA_NO_RUNTIME_LINKING)
    target_link_libraries(vox_miniaudio PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(vox_miniaudio PUBLIC m ${CMAKE_DL_LIBS})
endif()
vox_apply_sanitizers(vox_miniaudio)

add_library(vox_devices STATIC
    src/audio_file.cpp
    src/fake_backend.cpp
    src/miniaudio_backend.cpp
    src/miniaudio_errors.cpp
    src/virtual_cable.cpp)
add_library(vox::devices ALIAS vox_devices)
target_include_directories(vox_devices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vox_devices PUBLIC vox::core PRIVATE vox::dsp vox_miniaudio)
vox_target_defaults(vox_devices)

if(VOX_BUILD_TESTS)
    add_executable(vox_devices_tests
        tests/audio_file_tests.cpp
        tests/backend_tests.cpp
        tests/miniaudio_error_tests.cpp)
    # The error mapping is internal to the backend; its test reaches into src/.
    target_include_directories(vox_devices_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vox_devices_tests PRIVATE vox::devices vox::testing vox_miniaudio
        Catch2::Catch2WithMain)
    vox_target_defaults(vox_devices_tests)
    catch_discover_tests(vox_devices_tests PROPERTIES LABELS devices)
endif()
