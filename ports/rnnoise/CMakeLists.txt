cmake_minimum_required(VERSION 3.21)
project(rnnoise C)

add_library(rnnoise STATIC
    src/celt_lpc.c src/denoise.c src/kiss_fft.c src/pitch.c
    src/rnn.c src/rnn_data.c src/rnn_reader.c)
target_include_directories(rnnoise
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>
    PRIVATE src)
target_compile_definitions(rnnoise PRIVATE RNNOISE_BUILD)
if(MSVC)
    target_compile_definitions(rnnoise PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
    target_link_libraries(rnnoise PRIVATE m)
endif()
set_target_properties(rnnoise PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS rnnoise EXPORT rnnoiseTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES include/rnnoise.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT rnnoiseTargets NAMESPACE rnnoise:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rnnoise)
configure_package_config_file(rnnoise-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/rnnoise-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rnnoise)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/rnnoise-config.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/rnnoise)
