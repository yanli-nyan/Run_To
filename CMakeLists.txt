cmake_minimum_required(VERSION 3.16)

project(Run_To VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Quick Mqtt Widgets QuickControls2)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(Run_To
    src/main.cpp
    src/models/ModuleModel.cpp
    src/controllers/MqttHandler.cpp
    src/controllers/AppIconProvider.cpp
    src/controllers/SoftwareScanner.cpp
    src/controllers/ShareManager.cpp
    src/controllers/AppLauncher.cpp
    src/controllers/ConfigLauncher.cpp
    src/controllers/FileRevealer.cpp
    src/controllers/JsonCommandDispatcher.cpp
    src/controllers/ShortcutController.cpp
    src/controllers/SystemController.cpp
)

qt_add_qml_module(Run_To
    URI Run_To
    QML_FILES
        qml/Main.qml
        qml/views/SettingWindow.qml
        qml/views/DetailPopup.qml
        qml/components/ActionCard.qml
        qml/views/SelectAppPage.qml
        qml/views/DetailAppPage.qml
        qml/views/ModuleManagerDialog.qml
        qml/views/ShortcutConfigDialog.qml

    SOURCES
        src/models/ModuleModel.h    
        src/controllers/MqttHandler.h
        src/controllers/SoftwareScanner.h
        src/controllers/AppIconProvider.h
        src/controllers/ShareManager.h
        src/controllers/AppLauncher.h
        src/controllers/ConfigLauncher.h
        src/controllers/FileRevealer.h
        src/controllers/JsonCommandDispatcher.h
        src/controllers/ShortcutController.h
        src/controllers/SystemController.h
        src/controllers/ConfigPaths.h

    RESOURCES
        assets/Run.png


)

target_include_directories(Run_To
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models
)

set_target_properties(Run_To PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(Run_To
    PRIVATE
        Qt6::Quick
        Qt6::Mqtt
        Qt6::Widgets
        Qt6::QuickControls2
        Qt6::Gui
)

if(WIN32)
    target_link_libraries(Run_To
        PRIVATE
            ole32
            shell32
    )
endif()

include(GNUInstallDirs)
install(TARGETS Run_To
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ---------------------------------------------------------------------------
# 官方 QML 部署脚本。
# 本项目用 qt_add_qml_module 把 QML 编译进 qrc，windeployqt 无法扫描 qrc
# 内部的 import，因此单独运行 windeployqt 得到的目录里 qml/ 是空的、
# 程序启动会报 “module QtQuick.Controls plugin not found”。
# 该脚本在 `cmake --install <build-dir> --prefix <dist-dir>` 时正确
# 复制 Qt 运行库、平台插件与全部 QML 模块。
# ---------------------------------------------------------------------------
if(WIN32)
    qt_generate_deploy_qml_app_script(
        TARGET Run_To
        OUTPUT_SCRIPT deploy_qml_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_qml_script})
endif()

