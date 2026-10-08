# Internal development tools. Not installed, not shipped, not documented as
# product features.

if(NOT TARGET vox::testing)
    message(STATUS "Tools need the test support library; skipping (VOX_BUILD_TESTS=OFF).")
    return()
endif()

add_executable(vox_bench_pitch bench/pitch_engines.cpp)
target_link_libraries(vox_bench_pitch PRIVATE vox::dsp vox::testing)
vox_target_defaults(vox_bench_pitch)

add_executable(vox_bench_noise bench/noise_suppression.cpp)
target_link_libraries(vox_bench_noise PRIVATE vox::dsp vox::testing)
vox_target_defaults(vox_bench_noise)

add_executable(vox_voice_report bench/voice_report.cpp ${PROJECT_SOURCE_DIR}/plugins/tests/voice_metrics.cpp)
target_include_directories(vox_voice_report PRIVATE ${PROJECT_SOURCE_DIR}/plugins/tests)
target_link_libraries(vox_voice_report PRIVATE vox::plugins vox::testing)
vox_target_defaults(vox_voice_report)
