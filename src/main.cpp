#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQmlContext>
#include <QDir>
#include <cstdio>
#include <windows.h>

#include "ModuleModel.h"
#include "SoftwareScanner.h"
#include "AppIconProvider.h"
#include "MqttHandler.h"
#include "ShareManager.h"
#include "FileRevealer.h"
#include "ShortcutController.h"

static void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray utf8Msg = msg.toUtf8();
    const char *file = context.file ? context.file : "";
    const char *function = context.function ? context.function : "";
    switch (type)
    {
    case QtDebugMsg:
    case QtInfoMsg:
        fprintf(stderr, "%s\n", utf8Msg.constData());
        break;
    case QtWarningMsg:
        fprintf(stderr, "[Warning] %s (%s:%u)\n", utf8Msg.constData(), file, context.line);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "[Critical] %s (%s:%u, %s)\n", utf8Msg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "[Fatal] %s (%s:%u, %s)\n", utf8Msg.constData(), file, context.line, function);
        fflush(stderr);
        abort();
    }
    fflush(stderr);
}

int main(int argc, char *argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    qInstallMessageHandler(messageHandler);

    // Qt.labs.platform 的 SystemTrayIcon / Menu 在 Windows 上由 QWidgetPlatform 实现，
    // 它要求进程中存在真正的 QApplication（仅 QGuiApplication 会导致
    // "No native SystemTrayIcon/Menu implementation available"）。
    QApplication app(argc, argv);
    
    app.setQuitOnLastWindowClosed(false);

    // 启用纯 GPU 绘制且支持高度定制的基础控件样式
    QQuickStyle::setStyle("Basic");

    app.setOrganizationName("MyHomeAutomation");
    app.setApplicationName("DesktopMqttLauncher");

    QQmlApplicationEngine engine;

    // 1. 数据模型
    ModuleModel myModel;
    // 首次启动时自动创建 exe 上一级目录下的 config 文件夹
    myModel.ensureConfigDir();
    myModel.loadDataFromSource();
    engine.rootContext()->setContextProperty("moduleModel", &myModel);

    // 2. 控制器与服务
    SoftwareScanner scannerpath;
    engine.rootContext()->setContextProperty("softwareScanner", &scannerpath);

    ShareManager shareManager;
    engine.rootContext()->setContextProperty("shareManager", &shareManager);

    // MqttHandler 内部持有唯一的 ShortcutController 实例（MQTT 指令与界面共用同一份 key.json 状态）
    MqttHandler mqttHandler;
    engine.rootContext()->setContextProperty("mqttHandler", &mqttHandler);
    engine.rootContext()->setContextProperty("shortcutController", mqttHandler.shortcutController());

    FileRevealer fileRevealer;
    engine.rootContext()->setContextProperty("fileRevealer", &fileRevealer);

    // 3. 图标 Provider 注册
    engine.addImageProvider("appicon", new AppIconProvider());

    // 4. 加载界面
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []()
        { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.loadFromModule("Run_To", "Main");

    // 本程序 99% 的时间都藏在托盘里。默认情况下 Qt 会一直保留 D3D11 设备、
    // 纹理与交换链，窗口隐藏后私有内存几乎不回落（实测 visible 153.4MB /
    // tray 152.7MB，等于没有释放）。下面两行让窗口隐藏时主动释放场景图与
    // 图形后端，代价仅是下次唤回时重建一次（几十毫秒，交互上无感）。
    //
    // 注意：这两个是 QQuickWindow 的实例方法而非静态方法，
    // 必须在根窗口创建之后调用。子窗口（设置/扫描等）由 QML 创建，
    // 这里通过窗口的 persistentGraphics 属性统一下发。
    for (QObject *rootObject : engine.rootObjects())
    {
        if (auto *window = qobject_cast<QQuickWindow *>(rootObject))
        {
            window->setPersistentGraphics(false);
            window->setPersistentSceneGraph(false);
        }
    }

    // 5. 触发 MQTT 自连（此时 QML Connections 已完成绑定，可正常接收信号）
    mqttHandler.tryAutoConnect();

    return app.exec();
}