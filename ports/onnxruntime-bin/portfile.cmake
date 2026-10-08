# Installs Microsoft's prebuilt ONNX Runtime instead of building it: the
# source build needs abseil, protobuf, and Eigen and takes over an hour,
# while the release archives are what ONNX Runtime itself recommends for
# applications. Each archive is pinned by its SHA-512.
set(VCPKG_BUILD_TYPE release)
set(VCPKG_POLICY_SKIP_ARCHITECTURE_CHECK enabled)
set(VCPKG_POLICY_SKIP_DUMPBIN_CHECKS enabled)
set(VCPKG_POLICY_DLLS_IN_STATIC_LIBRARY enabled)

set(ORT_VERSION 1.23.2)
if(VCPKG_TARGET_IS_WINDOWS)
    set(archive "onnxruntime-win-x64-${ORT_VERSION}.zip")
    set(hash 3f3aecea57a38fca987401306e72cde4058828a41ee57e16cc9ff3363eaaba16c43ae65545a280f0fd485da535d6e18541838cf5ccd12c13374f8f0a66704d0d)
elseif(VCPKG_TARGET_IS_OSX)
    set(archive "onnxruntime-osx-universal2-${ORT_VERSION}.tgz")
    set(hash aef38d651cceb77ded01ff02453bed4f8e51deac86bc053c237ceaa5af3da3c3e2fc2214a333bfee1f8b9ac6cb720bc1944575f076739ea36e7c38f47f46bbc8)
else()
    set(archive "onnxruntime-linux-x64-${ORT_VERSION}.tgz")
    set(hash ac836c937ec30aecad03360ebc338338641a3421143d51c8eb45c71b346fd6e3ab3f680ce52ee99582cee27c051418789064543ec3361d574b1940c9c49a8a7c)
endif()

vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/${archive}"
    FILENAME "${archive}"
    SHA512 ${hash}
)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")

file(INSTALL "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/onnxruntime")
if(VCPKG_TARGET_IS_WINDOWS)
    file(INSTALL "${SOURCE_PATH}/lib/onnxruntime.lib" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    file(INSTALL "${SOURCE_PATH}/lib/onnxruntime.dll" "${SOURCE_PATH}/lib/onnxruntime_providers_shared.dll"
         DESTINATION "${CURRENT_PACKAGES_DIR}/bin")
else()
    file(GLOB libraries "${SOURCE_PATH}/lib/libonnxruntime*")
    file(INSTALL ${libraries} DESTINATION "${CURRENT_PACKAGES_DIR}/lib" FOLLOW_SYMLINK_CHAIN)
endif()

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/onnxruntime-bin-config.cmake"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/ThirdPartyNotices.txt")
