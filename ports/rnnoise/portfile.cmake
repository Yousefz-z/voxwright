vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/xiph/rnnoise.git
    REF 6cbfd53eb348a8d394e0757b4025c6ded34eb2b6 # tag v0.1.1
)

# MSVC has no variable length arrays. The only caller passes n <= 864 with no
# window, so a fixed buffer is equivalent.
file(READ "${SOURCE_PATH}/src/celt_lpc.c" _lpc)
string(REPLACE "opus_val16 xx[n];" "opus_val16 xx[2048];\n   celt_assert(n <= 2048);" _lpc "${_lpc}")
file(WRITE "${SOURCE_PATH}/src/celt_lpc.c" "${_lpc}")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/rnnoise-config.cmake.in" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/rnnoise)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
