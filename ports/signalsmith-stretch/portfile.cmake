set(VCPKG_BUILD_TYPE release)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    REF a670068d9aeb64913331d5cc29337b19a457a7df # tag 1.4.0
)

file(INSTALL "${SOURCE_PATH}/signalsmith-stretch.h"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/signalsmith-stretch")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/signalsmith-stretch-config.cmake"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.txt")
