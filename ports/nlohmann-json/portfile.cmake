set(VCPKG_BUILD_TYPE release)

vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL https://github.com/nlohmann/json.git
    REF 55f93686c01528224f448c19128836e7df245f72 # tag v3.12.0
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DJSON_Install=ON
        -DJSON_MultipleHeaders=ON
        -DJSON_BuildTests=OFF
        -DJSON_ImplicitConversions=OFF
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME nlohmann_json CONFIG_PATH share/cmake/nlohmann_json)
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/pkgconfig")

# On MSVC the exported target lists the debugger visualizer as an interface
# source at the install root. Move it next to the CMake config, as vcpkg's own
# port does, so consumers find it. Other platforms do not install the file.
vcpkg_replace_string(
    "${CURRENT_PACKAGES_DIR}/share/nlohmann_json/nlohmann_jsonTargets.cmake"
    "{_IMPORT_PREFIX}/nlohmann_json.natvis"
    "{_IMPORT_PREFIX}/share/nlohmann_json/nlohmann_json.natvis"
    IGNORE_UNCHANGED
)
if(EXISTS "${CURRENT_PACKAGES_DIR}/nlohmann_json.natvis")
    file(RENAME
        "${CURRENT_PACKAGES_DIR}/nlohmann_json.natvis"
        "${CURRENT_PACKAGES_DIR}/share/nlohmann_json/nlohmann_json.natvis")
endif()
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.MIT")
