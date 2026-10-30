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
    src/controllers/FeishuConfig.cpp
    src/controllers/FeatureMap.cpp
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
        src/controllers/FeishuConfig.h
        src/controllers/FeatureMap.h
        src/controllers/ConfigPaths.h

    RESOURCES
        assets/Run.png


)

target_include_directories(Run_To
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/controllers
        ${CMAKE_CURRENT_SOURCE_DIR}/src/models
)

# 源文件统一是「无 BOM 的 UTF-8」，且大量注释为中文。
# 不加这个标志时 MSVC 按系统代码页（中文机器为 936）解读源文件，
# 每个含中文的 .cpp/.h 都会抛一次 C4819，编译日志被刷满。
# /utf-8 等价于 /source-charset:utf-8 + /execution-charset:utf-8，
# 让源码按 UTF-8 读取、字符串字面量也按 UTF-8 落地，与 Qt 语义一致。
if(MSVC)
    target_compile_options(Run_To PRIVATE /utf-8)
endif()

# Release 构建下掐掉 qDebug()：项目里有 80+ 处 qDebug/qInfo 调用，
# 每条 MQTT 报文都会拼字符串。QT_NO_DEBUG_OUTPUT 让 qDebug 直接变成空操作，
# 不进入 Qt 日志系统，也就不会为日志格式化分配内存。
# qWarning/qCritical 保留，故障排查场景仍需要。
target_compile_definitions(Run_To
    PRIVATE
        $<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT>
        $<$<CONFIG:MinSizeRel>:QT_NO_DEBUG_OUTPUT>
        $<$<CONFIG:RelWithDebInfo>:QT_NO_DEBUG_OUTPUT>
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