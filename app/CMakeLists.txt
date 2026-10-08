# The desktop application: Qt Quick UI over the engine.
find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Core Gui Qml Quick QuickControls2 Svg Test)
qt_standard_project_setup(REQUIRES 6.8)

# Everything but main(): controllers, models, settings, and the QML module.
# A static library so the tests link exactly what the app runs.
qt_add_library(vox_app_core STATIC
    src/app_context.cpp
    src/app_context.hpp
    src/audio_controller.cpp
    src/audio_controller.hpp
    src/device_list_model.cpp
    src/device_list_model.hpp
    src/hotkey_controller.cpp
    src/hotkey_controller.hpp
    src/hotkeys/global_hotkeys.cpp
    src/hotkeys/global_hotkeys.hpp
    src/hotkeys/hotkey_keys.cpp
    src/hotkeys/hotkey_keys.hpp
    src/notification_model.cpp
    src/notification_model.hpp
    src/settings.cpp
    src/settings.hpp
    src/soundboard_controller.cpp
    src/soundboard_controller.hpp
    src/soundboard_store.cpp
    src/soundboard_store.hpp
    src/voice_controller.cpp
    src/voice_controller.hpp
    src/voice_models.cpp
    src/voice_models.hpp)
target_include_directories(vox_app_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(vox_app_core PUBLIC VOX_APP_VERSION="${PROJECT_VERSION}")
target_link_libraries(vox_app_core PUBLIC
    Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick vox::engine vox::plugins vox::devices)
vox_target_defaults(vox_app_core)

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
        qml/components/DevicePicker.qml
        qml/components/Icon.qml
        qml/components/LabeledSlider.qml
        qml/components/LevelMeter.qml
        qml/components/HotkeyField.qml
        qml/components/NavButton.qml
        qml/components/PageButton.qml
        qml/components/SearchField.qml
        qml/components/SoundEditor.qml
        qml/components/SoundTile.qml
        qml/components/SwitchRow.qml
        qml/components/ToggleChip.qml
        qml/components/VoiceTile.qml
        qml/pages/AudioPage.qml
        qml/pages/HotkeysPage.qml
        qml/pages/SoundboardPage.qml
        qml/pages/VoicesPage.qml
    RESOURCES
        icons/app.svg)

qt_add_executable(voxwright WIN32 MACOSX_BUNDLE src/main.cpp)
target_link_libraries(voxwright PRIVATE vox_app_core vox_app_coreplugin Qt6::QuickControls2 Qt6::Svg)
vox_target_defaults(voxwright)
set_target_properties(voxwright PROPERTIES
    OUTPUT_NAME Voxwright
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/platform/macos/Info.plist.in
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.yousefz-z.voxwright
    MACOSX_BUNDLE_BUNDLE_NAME Voxwright
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

if(VOX_BUILD_TESTS)
    qt_add_executable(vox_app_tests
        tests/app_test_main.cpp
        tests/app_test_support.hpp
        tests/controller_tests.cpp
        tests/hotkey_tests.cpp
        tests/model_tests.cpp
        tests/settings_tests.cpp
        tests/soundboard_tests.cpp
        tests/ui_smoke_tests.cpp)
    target_link_libraries(vox_app_tests PRIVATE
        vox_app_core vox_app_coreplugin Qt6::QuickControls2 Qt6::Svg Qt6::Test vox::testing
        Catch2::Catch2)
    vox_target_defaults(vox_app_tests)
    # The tests run without a display. On Windows the Qt DLLs must be on the
    # path when CTest lists and runs them.
    set(_qt_bin "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
    message(STATUS "Qt binaries for app tests: ${_qt_bin}")
    catch_discover_tests(vox_app_tests
        PROPERTIES LABELS app ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        DL_PATHS "${_qt_bin}")
endif()
