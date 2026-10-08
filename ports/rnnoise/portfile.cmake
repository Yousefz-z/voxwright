vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/xiph/rnnoise.git
    REF 6cbfd53eb348a8d394e0757b4025c6ded34eb2b6 # tag v0.1.1
)

# MSVC has no variable length arrays. Each function patched here has a single
# caller, in denoise.c, that passes constants, so a fixed buffer at least as
# large is equivalent: celt_lpc.c needs n <= 864, and pitch.c needs 240
# (x_lp4), 387 (y_lp4), 294 (xcorr), and 385 (yy_lookup). Every pattern is a
# single line so it matches whatever line endings the checkout uses.
function(vox_rnnoise_patch file from to)
    file(READ "${SOURCE_PATH}/${file}" text)
    string(REPLACE "${from}" "${to}" patched "${text}")
    if(patched STREQUAL text)
        message(FATAL_ERROR "The rnnoise patch for ${file} no longer applies: ${from}")
    endif()
    file(WRITE "${SOURCE_PATH}/${file}" "${patched}")
endfunction()
vox_rnnoise_patch(src/celt_lpc.c "opus_val16 xx[n];"
    "opus_val16 xx[2048];\n   celt_assert(n <= 2048);")
vox_rnnoise_patch(src/pitch.c "opus_val16 x_lp4[len>>2];" "opus_val16 x_lp4[512];")
vox_rnnoise_patch(src/pitch.c "opus_val16 y_lp4[lag>>2];" "opus_val16 y_lp4[1024];")
vox_rnnoise_patch(src/pitch.c "opus_val32 xcorr[max_pitch>>1];"
    "opus_val32 xcorr[512];\n   celt_assert((len>>2) <= 512 && (lag>>2) <= 1024 && (max_pitch>>1) <= 512);")
vox_rnnoise_patch(src/pitch.c "opus_val32 yy_lookup[maxperiod+1];"
    "opus_val32 yy_lookup[1024];\n   celt_assert(maxperiod+1 <= 1024);")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/rnnoise-config.cmake.in" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/rnnoise)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include" "${CURRENT_PACKAGES_DIR}/debug/share")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/COPYING")
