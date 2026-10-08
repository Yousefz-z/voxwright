set(VCPKG_BUILD_TYPE release)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/Signalsmith-Audio/linear.git
    REF de55e6a50ffcf6f8f43f649692d94691c7025151 # tag 0.6.4
)

# The library's real headers live at the repository root and include each other
# relatively, so they are installed together under include/signalsmith-linear.
file(INSTALL "${SOURCE_PATH}/approx.h" "${SOURCE_PATH}/fft.h" "${SOURCE_PATH}/linear.h"
    "${SOURCE_PATH}/stft.h" "${SOURCE_PATH}/platform"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/signalsmith-linear")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/signalsmith-linear-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
