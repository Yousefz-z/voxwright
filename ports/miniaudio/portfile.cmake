# Header-only install. The implementation is compiled once inside the devices module.
set(VCPKG_BUILD_TYPE release)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/mackron/miniaudio.git
    REF 9634bedb5b5a2ca38c1ee7108a9358a4e233f14d # tag 0.11.25
)

file(INSTALL "${SOURCE_PATH}/miniaudio.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/extras/stb_vorbis.c" DESTINATION "${CURRENT_PACKAGES_DIR}/include/miniaudio-extras")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/miniaudio-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
