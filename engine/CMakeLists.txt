add_library(vox_engine STATIC
    src/audio_engine.cpp
    src/output_stage.cpp
    src/processing_graph.cpp
    src/soundboard_player.cpp
    src/transmit_control.cpp)
add_library(vox::engine ALIAS vox_engine)
target_include_directories(vox_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vox_engine PUBLIC vox::core vox::dsp vox::plugins vox::devices)
vox_target_defaults(vox_engine)

if(VOX_BUILD_TESTS)
    add_executable(vox_engine_tests
        tests/audio_engine_tests.cpp
        tests/drift_simulation.cpp
        tests/engine_thread_tests.cpp
        tests/output_stage_tests.cpp
        tests/processing_graph_tests.cpp
        tests/soundboard_tests.cpp
        tests/transmit_tests.cpp)
    target_link_libraries(vox_engine_tests PRIVATE vox::engine vox::testing Catch2::Catch2WithMain)
    vox_target_defaults(vox_engine_tests)
    catch_discover_tests(vox_engine_tests PROPERTIES LABELS engine)
endif()
