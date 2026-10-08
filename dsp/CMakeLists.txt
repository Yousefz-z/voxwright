find_package(signalsmith-stretch CONFIG REQUIRED)

find_package(rnnoise CONFIG REQUIRED)

find_package(SampleRate CONFIG REQUIRED)

add_library(vox_dsp STATIC
    src/ambience.cpp
    src/biquad.cpp
    src/distortion.cpp
    src/dynamics.cpp
    src/echo.cpp
    src/equalizer.cpp
    src/meters.cpp
    src/modulation.cpp
    src/multimode_filter.cpp
    src/noise_suppressor.cpp
    src/oversampler.cpp
    src/pitch_tracker.cpp
    src/psola.cpp
    src/resampler.cpp
    src/resonator.cpp
    src/ring_modulator.cpp
    src/spectral_shifter.cpp
    src/vocoder.cpp
    src/whisper.cpp)
add_library(vox::dsp ALIAS vox_dsp)
target_include_directories(vox_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vox_dsp PUBLIC vox::core PRIVATE signalsmith::stretch rnnoise::rnnoise SampleRate::samplerate)
vox_target_defaults(vox_dsp)

if(VOX_BUILD_TESTS)
    add_executable(vox_dsp_tests
        tests/dynamics_tests.cpp
        tests/effects_tests.cpp
        tests/filter_tests.cpp
        tests/pitch_shifter_tests.cpp
        tests/pitch_tracker_tests.cpp
        tests/realtime_tests.cpp
        tests/resampler_tests.cpp)
    target_link_libraries(vox_dsp_tests PRIVATE vox::dsp vox::testing Catch2::Catch2WithMain)
    vox_target_defaults(vox_dsp_tests)
    catch_discover_tests(vox_dsp_tests PROPERTIES LABELS dsp)
endif()
