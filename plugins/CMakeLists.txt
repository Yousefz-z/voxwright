find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB VOX_VOICE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/voices/*.json)
list(SORT VOX_VOICE_FILES)
set(VOX_VOICES_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/builtin_voices_data.cpp)
add_custom_command(
    OUTPUT ${VOX_VOICES_SOURCE}
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${VOX_VOICES_SOURCE}
        -DHEADER=vox/plugins/voice_library.hpp
        -DFUNCTION=builtinVoiceSources
        "-DFILES=${VOX_VOICE_FILES}"
        -P ${PROJECT_SOURCE_DIR}/cmake/EmbedTextFiles.cmake
    DEPENDS ${VOX_VOICE_FILES} ${PROJECT_SOURCE_DIR}/cmake/EmbedTextFiles.cmake
    COMMENT "Embedding built-in voices"
    VERBATIM)

add_library(vox_plugins STATIC
    src/builtin_effects.cpp
    src/effect.cpp
    src/registry.cpp
    src/voice_chain.cpp
    src/voice_library.cpp
    src/voice_preset.cpp
    ${VOX_VOICES_SOURCE})
add_library(vox::plugins ALIAS vox_plugins)
target_include_directories(vox_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vox_plugins PUBLIC vox::core vox::dsp PRIVATE nlohmann_json::nlohmann_json)
vox_target_defaults(vox_plugins)

if(VOX_BUILD_TESTS)
    add_executable(vox_plugins_tests
        tests/preset_tests.cpp
        tests/registry_tests.cpp
        tests/voice_metrics.cpp
        tests/voice_tests.cpp)
    target_link_libraries(vox_plugins_tests PRIVATE vox::plugins vox::testing Catch2::Catch2WithMain)
    vox_target_defaults(vox_plugins_tests)
    catch_discover_tests(vox_plugins_tests PROPERTIES LABELS plugins)
endif()
