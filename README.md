# Run_To

[中文](README.md) | [English](README_EN.md)

**Run_To** 是一个基于 Qt Quick 的 Windows 桌面控制面板。它用于管理本地应用卡片、启动可执行文件或快捷方式，并接收 MQTT 指令（例如 NFC 自动化发出的指令）以启动已配置的应用。

## 功能

- 现代化 Qt Quick 控制面板和系统托盘：关闭窗口时仅隐藏到托盘，选择“彻底退出”才会结束进程。
- 添加、编辑、删除、浏览和启动本地应用条目，支持 `.exe` 与 `.lnk`。
- 扫描 Windows 的应用程序目录和桌面，查找候选 `.exe`、`.lnk` 文件。
- 显示 Windows 原生应用图标（包括快捷方式图标）；无法提取时使用颜色和字符生成备用图标。
- 通过 `config/config.json` 保存模块定义，并使用 `QSaveFile` 原子写入更新。
- 连接 MQTT Broker，通过 `QSettings` 保存连接参数；已保存主机地址时启动后会自动连接。
- 以 QoS 1 订阅 `home/esp32`。
- 解析 JSON 负载的 `value` 字段，在界面显示收到的指令，并用它匹配模块的 `character` 标识来启动应用。
- 将 `config/config.json` 及各应用/备用图标 PNG 导出为 `config/share_bundle.zip`。

## 平台与依赖

当前源码使用 Windows API（`windows.h`、快捷方式处理、原生图标提取、控制台代码页），因此目标平台为 **Windows**。

- CMake 3.16 或更高版本
- 支持 C++17 的编译器（MSVC 2022，Qt Kit 使用 `msvc2022_64`）
- Qt **6.8 或更高版本**，并安装以下模块：
  - Quick
  - Quick Controls 2
  - MQTT
  - Widgets
- 可选：Ninja 构建工具
- 如需远程指令功能，还需可访问的 MQTT Broker

## 构建

在已配置 Qt 工具链的终端中执行。将 `<Qt-prefix>` 替换为 Qt Kit 目录，例如 `D:/Workspace/SDKs/Qt/6.8.3/msvc2022_64`：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt-prefix>
cmake --build build
```

可执行文件通常生成在构建目录中。若要生成可分发的 Windows 程序目录，请使用同一 Qt Kit 的 `windeployqt`；`config.json` 不再与可执行文件同目录，而是放在可执行文件上一级目录的 `config` 文件夹中（首次启动会自动创建）：

```powershell
<Qt-prefix>/bin/windeployqt.exe --release --qmldir qml --dir dist <path-to>/appRun_To.exe
```

也可以直接用 CMake 的安装步骤完成部署（`CMakeLists.txt` 已接入官方 QML 部署脚本，会自动复制运行库与全部 QML 模块）：

```powershell
cmake --install build --config Release --prefix dist
```

## 首次配置

1. 启动程序，打开“设置”。
2. 填写 MQTT Broker 主机、用户名和密码，点击“保存并连接”。当前界面固定使用 **1883** 端口。
3. 通过“添加更多”扫描或手动添加应用模块；也可以在“远程智能”中创建指令型模块。
4. 对需要启动本地程序的模块，设置唯一的 `character`，并填写 `.exe` 或 `.lnk` 的绝对路径。
5. 配置文件统一放在 **可执行文件上一级目录的 `config` 文件夹** 中（例如 `C:/App/bin/Run_To.exe` 对应 `C:/App/config/config.json`）。MQTT 指令查询、快捷键配置与导出功能都以该目录为基准，程序首次启动会自动创建该文件夹，无需手动放置。

### `config/config.json` 格式

`config/config.json` 由程序创建和维护，最小示例如下：

```json
{
  "version": "1.1",
  "modules": [
    {
      "name": "示例应用",
      "path": "C:/Program Files/Example/app.exe",
      "color": "#3B82F6",
      "character": "K1"
    }
  ]
}
```

每个模块都会保存 `name`、`color`、`character`；`path` 是可选字段，但要由指令启动应用时必须存在且指向有效文件。

## MQTT 指令协议

连接成功后，客户端会订阅固定主题：

```text
home/esp32
```

向此主题发布包含 `value` 字段的 JSON。其值会与模块的 `character` 精确匹配：

```json
{
  "value": "K1"
}
```

对于没有专用处理器的指令，程序会尝试寻找 `character` 与收到的 `value` 相同的模块并启动其路径。源码虽注册了 `REBOOT` 和 `VOLUME_MUTE` 指令，但当前仅输出已处理的日志，不会真的重启电脑或调整音量。

固定主题和 JSON 结构可配合将 NFC/设备事件转换为 MQTT 消息的自动化使用。

## 界面使用

- 点击应用卡片可查看或编辑名称、路径、备用图标字符和颜色，也可立即启动。
- “添加更多”会打开应用/快捷方式扫描器，并支持手动录入。
- “远程智能”创建没有本地路径的指令模块；如果该指令需要启动本机程序，后续请补充路径。
- “导出配置”默认在 `config` 文件夹中生成 `share_bundle.zip`，其中包括当前 `config/config.json` 和 `resources/<character>.png` 图标。
- 使用托盘图标可恢复窗口；直接关闭窗口会让程序继续驻留托盘。

## 安全注意事项

- MQTT 主机、用户名和密码通过 `QSettings` 保存，并非安全凭据库。请使用可信 Windows 帐户并保护 Broker。
- `config` 文件夹中的 `config.json`、`key.json` 与 `share_bundle.zip` 都可能暴露本地应用路径与模块标识符，应按敏感文件保管。
- 界面未提供 TLS 配置；连接会使用填写的主机和端口。请使用隔离的可信网络，或自行扩展加密 MQTT 支持。
- 任何有权向 `home/esp32` 发布消息的客户端，都可能请求启动已配置的本地应用。请严格配置 Broker ACL，且不要接受不可信指令源。
- 导出的配置包可能暴露本地应用路径和模块标识符，应按敏感文件保管。

## 项目结构

```text
src/
  main.cpp                         应用初始化与 QML 上下文对象
  models/ModuleModel.*             基于 config/config.json 的模块列表模型
  controllers/
    MqttHandler.*                  MQTT 连接和固定主题订阅
    JsonCommandDispatcher.*        JSON `value` 解析与指令分发
    ConfigLauncher.*               character 到配置路径的启动映射
    AppLauncher.*                  .exe / .lnk 启动器
    SoftwareScanner.*              Windows 应用/桌面扫描器
    ShareManager.*                 ZIP 配置导出
    AppIconProvider.*              Windows 应用图标提供器
qml/
  Main.qml                         主窗口与系统托盘行为
  views/                           设置、模块和应用对话框
  components/ActionCard.qml        可复用应用卡片
assets/Run.png                     托盘/应用图标
```
