# Run_To

[中文文档](README_CN.md) | [English](README.md)

**Run_To** is a Windows desktop control panel built with Qt Quick. It manages a set of application cards, launches local executables or shortcuts, and accepts MQTT commands—such as those emitted by an NFC automation—to launch a configured application.

## Features

- Modern Qt Quick control-panel UI with system-tray support; closing the window hides it to the tray, while **Quit** exits the application.
- Add, edit, remove, browse, and launch local application entries (`.exe` and `.lnk`).
- Scan the Windows Applications and Desktop locations for candidate `.exe` and `.lnk` files.
- Display native Windows application icons, including shortcut icons, with a generated letter/color fallback.
- Store module definitions in `config.json` and write updates atomically with `QSaveFile`.
- Connect to an MQTT broker, save broker settings through `QSettings`, and reconnect automatically when a saved host exists.
- Subscribe to `home/desktop/nfc_command` at QoS 1.
- Parse a JSON payload's `value` property, show the received command in the UI, and use it to match a configured module's `character` value.
- Export `config.json` plus PNG application/fallback icons to `share_bundle.zip`.

## Platform and requirements

The current source uses Windows APIs (`windows.h`, Shell links, native icon extraction, console code pages), so it is intended for **Windows**.

- CMake 3.16 or newer
- A C++17-capable compiler (the existing build cache uses MinGW)
- Qt **6.8 or newer** with the following components:
  - Quick
  - Quick Controls 2
  - MQTT
  - Widgets
- Optional: Ninja for faster builds
- An accessible MQTT broker when remote command handling is required

## Build

Open a terminal configured for your Qt toolchain, then configure and build. Replace `<Qt-prefix>` with the directory that contains your Qt kit, for example `D:/Qt/6.8.3/mingw_64`.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt-prefix>
cmake --build build
```

The executable is normally generated beneath the build directory. For a distributable Windows folder, use the `windeployqt` tool that belongs to the same Qt kit, then place `config.json` beside the executable.

```powershell
<Qt-prefix>/bin/windeployqt.exe <path-to>/appRun_To.exe
```

## First-run configuration

1. Start the application and open **Settings**.
2. Enter the MQTT broker host, username, and password, then choose **Save and connect**. The UI currently connects on port **1883**.
3. Add application modules from **Add more** (scan candidates or enter details manually), or use **Remote Smart** to create pathless command modules.
4. For a launchable module, set a unique `character` and the absolute path to an `.exe` or `.lnk` file.
5. Keep `config.json` in the application's working/executable directory. MQTT command lookup uses the executable directory; keeping the file beside the executable avoids path ambiguity.

### `config.json` format

`config.json` is created and updated by the application. A minimal example is:

```json
{
  "version": "1.1",
  "modules": [
    {
      "name": "Example application",
      "path": "C:/Program Files/Example/app.exe",
      "color": "#3B82F6",
      "character": "K1"
    }
  ]
}
```

`name`, `color`, and `character` are persisted for every module. `path` is optional, but must be present and valid for a command to launch an application.

## MQTT command protocol

After connecting, the client subscribes to the fixed topic:

```text
home/desktop/nfc_command
```

Publish a JSON object with a `value` field. Its value is matched exactly against a module's `character` property:

```json
{
  "value": "K1"
}
```

For commands without a dedicated handler, the application attempts to launch the module whose `character` matches the received `value`. The source also registers `REBOOT` and `VOLUME_MUTE` handlers, but they currently only log that the command was handled; they do not reboot the computer or change volume.

This fixed topic and payload shape are compatible with an automation that converts NFC/device events into MQTT messages.

## Using the UI

- Click an application card to view or edit its name, path, fallback icon character, and color; it can also be launched immediately.
- **Add more** opens a scanner for application shortcuts/executables and a form for manual entries.
- **Remote Smart** adds command-oriented modules without a local path. Add a path later if the command should launch a local program.
- **Export configuration** writes `share_bundle.zip` into the application directory by default. It contains the current `config.json` and `resources/<character>.png` images.
- Use the tray icon to restore the window. Closing the window leaves the process running in the tray.

## Security notes

- MQTT host, username, and password are saved through `QSettings`, which is not a secure credential vault. Use a trusted Windows account and secure the broker.
- The MQTT connection uses the configured host and port with no TLS settings exposed by the UI. Use an isolated trusted network, or extend the client to configure encrypted MQTT.
- Any publisher authorized to write to `home/desktop/nfc_command` can request a configured local application launch. Restrict broker ACLs and do not use untrusted command sources.
- Treat exported configuration bundles as sensitive: they may expose local application paths and module identifiers.

## Project layout

```text
src/
  main.cpp                         Application setup and QML context objects
  models/ModuleModel.*             config.json-backed module list model
  controllers/
    MqttHandler.*                  MQTT connection and fixed-topic subscription
    JsonCommandDispatcher.*        JSON `value` parsing and command dispatch
    ConfigLauncher.*               character-to-configured-path launcher
    AppLauncher.*                  .exe / .lnk launching
    SoftwareScanner.*              Windows application/Desktop scanner
    ShareManager.*                 ZIP configuration export
    AppIconProvider.*              Windows application icon provider
qml/
  Main.qml                         Main window and system tray behavior
  views/                           Settings, module, and application dialogs
  components/ActionCard.qml        Reusable application card
assets/Run.png                     Tray/application icon
```
