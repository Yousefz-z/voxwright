# The desktop application: Qt Quick UI over the engine.
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Concurrent Core Gui Multimedia Qml Quick QuickControls2 Svg Test TextToSpeech Widgets)
qt_standard_project_setup(REQUIRES 6.8)

# Everything but main(): controllers, models, settings, and the QML module.
# A static library so the tests link exactly what the app runs.
qt_add_library(vox_app_core STATIC
    src/app_context.cpp
    src/app_context.hpp
    src/audio_controller.cpp
    src/audio_controller.hpp
    src/custom_voice_store.cpp
    src/custom_voice_store.hpp
    src/designer_controller.cpp
    src/designer_controller.hpp
    src/device_list_model.cpp
    src/device_list_model.hpp
    src/hotkey_controller.cpp
    src/hotkey_controller.hpp
    src/hotkeys/global_hotkeys.cpp
    src/hotkeys/global_hotkeys.hpp
    src/hotkeys/hotkey_keys.cpp
    src/hotkeys/hotkey_keys.hpp
    src/neural_controller.cpp
    src/neural_controller.hpp
    src/notification_model.cpp
    src/notification_model.hpp
    src/platform/autostart.cpp
    src/platform/autostart.hpp
    src/settings.cpp
    src/settings.hpp
    src/soundboard_controller.cpp
    src/soundboard_controller.hpp
    src/soundboard_store.cpp
    src/soundboard_store.hpp
    src/speech_controller.cpp
    src/speech_controller.hpp
    src/system_controller.cpp
    src/system_controller.hpp
    src/tray_controller.cpp
    src/tray_controller.hpp
    src/virtual_mic_check.cpp
    src/virtual_mic_check.hpp
    src/voice_controller.cpp
    src/voice_controller.hpp
    src/voice_models.cpp
    src/voice_models.hpp)
target_include_directories(vox_app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(vox_app_core PUBLIC VOX_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(vox_app_core PUBLIC
    Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Multimedia Qt6::Qml Qt6::Quick Qt6::TextToSpeech
    Qt6::Widgets vox::engine vox::plugins vox::devices)
vox_target_defaults(vox_app_core)
if(VOX_ENABLE_ML)
    target_link_libraries(vox_app_core PUBLIC vox::ml)
endif()

# Global hotkeys: one implementation per platform.
if(WIN32)
    target_sources(vox_app_core PRIVATE src/hotkeys/hotkeys_windows.cpp)
elseif(APPLE)
    target_sources(vox_app_core PRIVATE src/hotkeys/hotkeys_macos.cpp)
    target_link_libraries(vox_app_core PRIVATE "-framework Carbon")
else()
    target_sources(vox_app_core PRIVATE src/hotkeys/hotkeys_unsupported.cpp)
endif()

set_source_files_properties(qml/Theme.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(vox_app_core
    URI Voxwright
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/Icons.qml
        qml/components/AppMark.qml
        qml/components/Banner.qml
        qml/components/Card.qml
        qml/components/CategoryChip.qml
        qml/components/ChoiceBox.qml
        qml/components/DevicePicker.qml
        qml/components/EffectBlockCard.qml
        qml/components/FirstRunGuide.qml
        qml/components/Icon.qml
        qml/components/IconButton.qml
        qml/components/LabeledSlider.qml
        qml/components/LevelMeter.qml
        qml/components/HotkeyField.qml
        qml/components/NavButton.qml
        qml/components/PageButton.qml
        qml/components/SearchField.qml
        qml/components/SoundEditor.qml
        qml/components/SoundTile.qml
        qml/components/SpeechPanel.qml
        qml/components/SwitchRow.qml
        qml/components/ToggleChip.qml
        qml/components/VirtualMicTest.qml
        qml/components/VoiceTile.qml
        qml/pages/AudioPage.qml
        qml/pages/DesignerPage.qml
        qml/pages/HotkeysPage.qml
        qml/pages/SettingsPage.qml
        qml/pages/SoundboardPage.qml
        qml/pages/VoicesPage.qml
    RESOURCES
        icons/app.svg)
# qmlcachegen compiles the QML files to C++. In optimized builds MSVC reports
# C4702 (unreachable code) inside the Qt headers that those files inline, so
# the warning is turned off for the qmlcachegen output only.
if(MSVC)
    get_target_property(VOX_QMLCACHE_SOURCES vox_app_core SOURCES)
    list(FILTER VOX_QMLCACHE_SOURCES INCLUDE REGEX "/\\.rcc/qmlcache/")
    set_source_files_properties(${VOX_QMLCACHE_SOURCES} PROPERTIES COMPILE_OPTIONS /wd4702)
endif()

qt_add_executable(voxwright WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(voxwright PRIVATE vox_app_core vox_app_coreplugin Qt6::QuickControls2 Qt6::Svg)
# Program icon and version information. The .ico and .icns files are made
# from icons/app.svg by tools/icons (see make_icons.py).
if(WIN32)
    set(VOX_WINDOWS_ICON ${CMAKE_CURRENT_SOURCE_DIR}/icons/voxwright.ico)
    configure_file(platform/windows/voxwright.rc.in ${CMAKE_CURRENT_BINARY_DIR}/voxwright.rc @ONLY)
    target_sources(voxwright PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/voxwright.rc)
elseif(APPLE)
    set(MACOSX_BUNDLE_ICON_FILE voxwright.icns)
    set_source_files_properties(icons/voxwright.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(voxwright PRIVATE icons/voxwright.icns)
endif()
vox_target_defaults(voxwright)
set_target_properties(voxwright PROPERTIES
    OUTPUT_NAME Voxwright
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.yousefz-z.voxwright
    MACOSX_BUNDLE_BUNDLE_NAME Voxwright
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

# Qt's QML import scan (which decides the QML modules to deploy) reruns only
# when its output is missing, and this app's QML lives in vox_app_core. Make
# the scan depend on the QML files so a changed import is deployed. Deferred
# so it runs after Qt has created the scan command.
file(GLOB_RECURSE vox_qml_sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml)
cmake_language(DEFER CALL add_custom_command
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/.qt/qml_imports/voxwright_build.cmake
    APPEND DEPENDS ${vox_qml_sources})

# Installing gives a self-contained program folder (Windows, Linux) or app
# bundle (macOS) with the Qt runtime deployed next to it, which the
# installers in installer/ package.
include(GNUInstallDirs)
install(TARGETS voxwright
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
qt_generate_deploy_qml_app_script(
    TARGET voxwright
    OUTPUT_SCRIPT vox_deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR)
install(SCRIPT ${vox_deploy_script})
if(APPLE)
    set(vox_licenses_dir Voxwright.app/Contents/Resources/licenses)
else()
    set(vox_licenses_dir licenses)
endif()
install(FILES
    ${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
    ${PROJECT_SOURCE_DIR}/licenses/LGPL-3.0-only.txt
    ${PROJECT_SOURCE_DIR}/licenses/GPL-3.0-only.txt
    DESTINATION ${vox_licenses_dir})
# License files of the libraries linked into the program, from vcpkg.
foreach(port libsamplerate miniaudio nlohmann-json rnnoise signalsmith-linear signalsmith-stretch)
    set(_copyright "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/${port}/copyright")
    if(EXISTS "${_copyright}")
        install(FILES "${_copyright}" DESTINATION ${vox_licenses_dir} RENAME ${port}.txt)
    endif()
endforeach()

if(VOX_BUILD_TESTS)
    qt_add_executable(vox_app_tests
        tests/app_test_main.cpp
        tests/app_test_support.hpp
        tests/controller_tests.cpp
        tests/designer_tests.cpp
        tests/hotkey_tests.cpp
        tests/mic_check_tests.cpp
        tests/neural_tests.cpp
        tests/model_tests.cpp
        tests/settings_tests.cpp
        tests/soundboard_tests.cpp
        tests/speech_tests.cpp
        tests/system_tests.cpp
        tests/ui_smoke_tests.cpp)
    target_link_libraries(vox_app_tests PRIVATE
        vox_app_core vox_app_coreplugin Qt6::QuickControls2 Qt6::Svg Qt6::Test vox::testing
        Catch2::Catch2)
    vox_target_defaults(vox_app_tests)
    if(VOX_ENABLE_ML)
        target_compile_definitions(vox_app_tests PRIVATE
            VOX_ML_TEST_MODELS="${PROJECT_SOURCE_DIR}/ml/tests/models")
    endif()
    # The tests run without a display. On Windows the Qt DLLs must be on the
    # path when CTest lists and runs them.
    set(_qt_bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    message(STATUS "Qt binaries for app tests: ${_qt_bin}")
    catch_discover_tests(vox_app_tests
        PROPERTIES LABELS app ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        DL_PATHS "${_qt_bin}")
endif()
