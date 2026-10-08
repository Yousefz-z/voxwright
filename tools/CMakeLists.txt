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

# Renders app/icons/app.svg for the .ico and .icns files (tools/icons).
if(VOX_BUILD_APP)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Gui Svg)
    add_executable(vox_render_icon icons/render_icon.cpp)
    target_link_libraries(vox_render_icon PRIVATE Qt6::Gui Qt6::Svg)
    vox_target_defaults(vox_render_icon)
endif()

add_executable(vox_bench_engine bench/engine_bench.cpp ${PROJECT_SOURCE_DIR}/engine/tests/drift_simulation.cpp)
target_include_directories(vox_bench_engine PRIVATE ${PROJECT_SOURCE_DIR}/engine/tests)
target_link_libraries(vox_bench_engine PRIVATE vox::engine vox::testing)
vox_target_defaults(vox_bench_engine)
