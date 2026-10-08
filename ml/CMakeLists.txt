# Optional neural voice conversion, built with -DVOX_ENABLE_ML=ON. Uses the
# prebuilt ONNX Runtime from the onnxruntime-bin overlay port (vcpkg
# feature "ml"). See docs/ml.md.
find_package(onnxruntime-bin CONFIG REQUIRED)

add_library(vox_ml STATIC
    src/neural_voice.cpp
    src/neural_model.cpp)
add_library(vox::ml ALIAS vox_ml)
target_include_directories(vox_ml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vox_ml PUBLIC vox::plugins vox::dsp vox::core PRIVATE onnxruntime::onnxruntime)
target_compile_definitions(vox_ml PUBLIC VOX_ENABLE_ML=1)
vox_target_defaults(vox_ml)

if(VOX_BUILD_TESTS)
    add_executable(vox_ml_tests tests/ml_tests.cpp)
    target_link_libraries(vox_ml_tests PRIVATE vox::ml vox::testing Catch2::Catch2WithMain)
    target_compile_definitions(vox_ml_tests PRIVATE
        VOX_ML_TEST_MODELS="${CMAKE_CURRENT_SOURCE_DIR}/tests/models")
    vox_target_defaults(vox_ml_tests)
    catch_discover_tests(vox_ml_tests PROPERTIES LABELS ml)
endif()
