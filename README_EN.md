# Run_To

[中文文档](README.md) | [English](README_EN.md)

**Run_To** is a Windows desktop control panel built with Qt Quick. It manages a set of application cards, launches local executables or shortcuts, and accepts MQTT commands—such as those emitted by an NFC automation—to launch a configured application.

## Features

- Modern Qt Quick control-panel UI with system-tray support; closing the window hides it to the tray, while **Quit** exits the application.
- Add, edit, remove, browse, and launch local application entries (`.exe` and `.lnk`).
- Scan the Windows Applications and Desktop locations for candidate `.exe` and `.lnk` files.
- Display native Windows application icons, including shortcut icons, with a generated letter/color fallback.
- Store module definitions in `config/config.json` and write updates atomically with `QSaveFile`.
- Connect to an MQTT broker, save broker settings through `QSettings`, and reconnect automatically when a saved host exists.
- Subscribe to `home/esp32` at QoS 1.
- Parse a JSON payload's `value` property, show the received command in the UI, and use it to match a configured module's `character` value.
- Export `config/config.json` plus PNG application/fallback icons to `config/share_bundle.zip`.

## Platform and requirements

The current source uses Windows APIs (`windows.h`, Shell links, native icon extraction, console code pages), so it is intended for **Windows**.

- CMake 3.16 or newer
- A C++17-capable compiler (MSVC 2022, Qt kit `msvc2022_64`)
- Qt **6.8 or newer** with the following components:
  - Quick
  - Quick Controls 2
  - MQTT
  - Widgets
- Optional: Ninja for faster builds
- An accessible MQTT broker when remote command handling is required

## Build

Open a terminal configured for your Qt toolchain, then configure and build. Replace `<Qt-prefix>` with the directory that contains your Qt kit, for example `D:/Workspace/SDKs/Qt/6.8.3/msvc2022_64`.

```powershell
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=<Qt-prefix>
cmake --build build
```

The executable is normally generated beneath the build directory. For a distributable Windows folder, use the `windeployqt` tool that belongs to the same Qt kit. `config.json` no longer sits beside the executable: it lives in a `config` folder one level above the executable (created automatically on first launch).

```powershell
<Qt-prefix>/bin/windeployqt.exe --release --qmldir qml --dir dist <path-to>/appRun_To.exe
```

Alternatively, deploy through the CMake install step (`CMakeLists.txt` wires the official QML deployment script, which copies the runtime and all QML modules):

```powershell
cmake --install build --config Release --prefix dist
```

## First-run configuration

1. Start the application and open **Settings**.
2. Enter the MQTT broker host, username, and password, then choose **Save and connect**. The UI currently connects on port **1883**.
3. Add application modules from **Add more** (scan candidates or enter details manually), or use **Remote Smart** to create pathless command modules.
4. For a launchable module, set a unique `character` and the absolute path to an `.exe` or `.lnk` file.
5. Configuration files live in the **`config` folder one level above the executable** (with `C:/App/bin/Run_To.exe`, that is `C:/App/config/config.json`). MQTT command lookup, shortcut settings, and export all use that folder; the application creates it on first launch, so nothing has to be placed by hand.

### `config/config.json` format

`config/config.json` is created and updated by the application. A minimal example is:

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

After connecting, the client subscribes to two command topics:

```text
home/esp32   device/NFC commands, legacy {"value":"K1"}
home/feishu  Feishu commands, action envelope, strict mode
```

### Legacy format (`home/esp32`)

Publish a JSON object with a `value` field. Its value is matched exactly against a module's `character` property:

```json
{
  "value": "K1"
}
```

For commands without a dedicated handler, the application attempts to launch the module whose `character` matches the received `value`. `REBOOT` and `VOLUME_MUTE` are now wired to real behaviour: the former schedules a restart with a 60 second countdown, the latter mutes the system.

### Envelope format (`home/feishu`)

```json
{
  "v": 1,
  "action": "power.restart",
  "params": { "delay": 60 },
  "msg_id": "om_xxxxxxxx",
  "from": "ou_yyyyyyyy",
  "ts": 1761726970
}
```

- `v`, `action`, `msg_id`, and `ts` are required; `params` defaults to `{}`.
- Commands older than **5 minutes** by `ts` are rejected with `expired`, so broker-queued commands from while the PC was offline do not fire on reconnect.
- A given `msg_id` is executed once; duplicates within the last 100 ids get `duplicate`.
- This topic runs in **strict mode**: only envelope payloads with `action` are accepted, and there is no `character` fallback matching.

Supported actions:

| action | params | Description |
| --- | --- | --- |
| `power.restart` | `{"delay": 60}` | Schedule a restart; a `delay` below 60 is raised to 60 |
| `power.shutdown` | `{"delay": 60}` | Schedule a shutdown, same rule |
| `power.cancel` | `{}` | Cancel a pending shutdown/restart (`shutdown /a`) |
| `power.lock` | `{}` | Lock the workstation |
| `power.sleep` | `{}` | Enter sleep |
| `app.launch` | `{"character": "N1"}` | Launch the program whose `character` matches in `config.json`; **paths never come from the payload** |
| `shortcut.run` | `{"key": "K3"}` | Run the shortcut with that id from `key.json` |
| `media.control` | `{"op": "mute"}` | `mute` / `play_pause` / `volume_up` / `volume_down` |
| `clipboard.set` | `{"text": "..."}` | Write text to the clipboard |
| `notify.show` | `{"text": "...", "level": "info"}` | Show a tray notification |
| `unknown` | `{"raw": "..."}` | Reported by the relay for unrecognised commands; logged only, never executed |

### Receipts and capabilities

Results are published to `home/feishu/ack` (retained, QoS 1) for the relay to forward back to Feishu:

```json
{ "v": 1, "msg_id": "om_xxx", "action": "power.restart", "status": "ok", "detail": "已计划 60 秒后重启", "ts": 1761726971 }
```

`status` is one of `ok` / `denied` / `expired` / `duplicate` / `failed` / `unknown`.

On connect the client also publishes a retained capability list to `home/feishu/caps`, so the relay can decide which actions to forward without hardcoding them:

```json
{ "v": 1, "actions": ["power.restart", "..."], "min_power_delay": 60 }
```

> Never publish command topics with `retain`. A retained command would be replayed and executed every time the PC comes online.

### Command table (`config/feishu/commands.json`)

The Feishu command word mapping is owned by the PC and stored in `config/feishu/commands.json` (the `config` folder one level above the executable). A ready-to-use default table is generated on first launch, and edits are hot-reloaded without restarting the application.

```json
{
  "schema": 1,
  "minPowerDelay": 60,
  "allowedSenders": ["ou_xxxxxxxx"],
  "unknownReply": "未识别的命令：{raw}",
  "commands": [
    {
      "id": "restart",
      "enabled": true,
      "match": { "type": "exact", "patterns": ["重启", "重启电脑"] },
      "action": "power.restart",
      "params": { "delay": 60 },
      "reply": "已计划 60 秒后重启"
    }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `schema` | Config format version, currently 1 |
| `minPowerDelay` | Countdown floor for power actions. The PC enforces a **hard floor of 30 seconds**; config can raise it but never lower it |
| `allowedSenders` | Feishu `open_id` values allowed to send commands; empty disables the check |
| `unknownReply` | Reply template for unrecognised commands; `{raw}` is replaced by the original text |
| `commands` | Command entries, **matched in order and the first hit wins**, so order expresses priority |

`match.type` supports three modes:

| type | Semantics | Example |
| --- | --- | --- |
| `exact` | Equal to one of `patterns` | "重启" |
| `prefix` | Starts with `pattern`; the remainder is stored under `captureAs` | "启动 N1" → `character=N1` |
| `regex` | Regex match; named capture groups merge into `params` | custom commands |

Normalisation only trims, collapses whitespace, converts full-width to half-width, and lowercases ASCII. There is deliberately **no natural-language understanding**.

After connecting, the PC publishes the whole table to `home/feishu/config` (retained, QoS 1). The relay subscribes, performs the text → action mapping, and caches it on disk so it keeps working while the PC is offline. The topic is republished whenever the file changes.

**Fault tolerance**: if the JSON is malformed, the PC keeps the last valid table and logs a warning instead of losing command capability. An invalid entry (duplicate `id`, unknown `action`, empty `patterns`) is skipped individually while the rest stays active.

### Command → feature map (`features`)

Chinese-to-command mapping happens on the Armbian side; the PC only decides what a command name actually does. That mapping lives in the same config file:

```json
"features": {
  "restart": { "type": "builtin", "target": "power.restart", "params": { "delay": 60 } },
  "deploy":  { "type": "script",  "target": "scripts/deploy.ps1", "params": {} }
}
```

| type | target | Meaning |
| --- | --- | --- |
| `builtin` | Action name (e.g. `power.restart`) | Runs the built-in capability; the name must exist in the built-in action table |
| `script` | Script path relative to `config` | Runs an external script; `.ps1`, `.bat`, `.cmd`, `.exe` are supported |
| `action` | Action name | Same as `builtin`, kept for compatibility with relays sending actions directly |

Notes:

- **Commands absent from `features` fall back to the built-in defaults** (restart / shutdown / cancel / lock / sleep / mute / launch / notify), so the application still works when the config file is missing.
- Script parameters are appended as `--name=value` and **never go through a shell**, so quotes or spaces in command words cannot become extra commands.
- Script extensions are whitelisted; `.dll` and `.msi` are deliberately excluded so a broken config cannot become an arbitrary file execution entry point.
- To add a feature: drop a script into `config/feishu/scripts/` and add an entry under `features`. **No code change, no rebuild, no restart** — the file is hot-reloaded on save.

#### How to fill in the script path

`target` accepts **either form** — an absolute path is not required:

| Value | Example | Resolved to |
| --- | --- | --- |
| Relative to the `config` folder | `"scripts/deploy.ps1"` | `config/scripts/deploy.ps1` |
| Relative to `config/feishu` | `"feishu/scripts/deploy.ps1"` | `config/feishu/scripts/deploy.ps1` |
| Absolute path | `"D:/Tools/deploy.ps1"` | used as-is |

The rule is simply: **a relative path is resolved against the `config` folder** (`FeatureMap::resolveScriptPath`). A missing file is not rejected at load time; execution returns `脚本不存在: <resolved absolute path>`, which is the fastest way to debug a wrong path.

#### Copy-paste example

`config/feishu/commands.json`:

```json
{
  "schema": 1,
  "features": {
    "cleanup": { "type": "script", "target": "scripts/cleanup.ps1", "params": { "days": 30 } },
    "backup":  { "type": "script", "target": "D:/Tools/backup.bat" }
  }
}
```

`config/scripts/cleanup.ps1`:

```powershell
param([int]$days = 7)
# Parameters arrive as --days=30; use param() or read $args directly
Write-Output "Cleaning files older than $days days"
Get-ChildItem $env:TEMP -Recurse -File |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-$days) } |
    Remove-Item -Force -ErrorAction SilentlyContinue
exit 0
```

Trigger it:

```powershell
# A. Legacy value branch — the command name matches a key under features
mosquitto_pub -h <broker> -t home/esp32 -m '{"value":"cleanup"}'
```

Note that A only works on `home/esp32`; `home/feishu` is strict mode and accepts envelopes only. To reach a script from Feishu, map a Chinese phrase to a whitelisted `action` in `commands` — a script feature itself is reachable through the `value` branch or indirectly via `shortcut.run`.

#### Script execution contract

- Suffix whitelist: `.ps1`, `.bat`, `.cmd`, `.exe`; anything else is rejected at load time and recorded as an error;
- `.ps1` runs through `powershell.exe -NoProfile -ExecutionPolicy Bypass -File`, so the machine execution policy does not need changing;
- the working directory is set to the script's folder, so relative paths inside the script work;
- parameters are appended as `--name=value` and never go through a shell;
- **30 second** timeout — the process is killed and the relay gets `脚本执行超时（30 秒）`; a non-zero exit code is a failure and up to 200 characters of stderr are put into the acknowledgement;
- for `.exe` the program is started directly with the same `--name=value` arguments.

## Shortcut configuration (`config/key.json`)

The **Shortcuts** dialog assigns one action to each of the six macro-keyboard keys K1–K6: combo shortcut, launch application, send text, or media control. The result is stored in `config/key.json`, and the MQTT command `shortcut.run` with `{"key":"K3"}` executes the same configuration.

For a combo shortcut, `param` is a `+`-separated list of **English key names**:

| Value | Meaning |
| --- | --- |
| `Ctrl + C` | Copy |
| `Win + D` | Show desktop |
| `Win + R` | Open "Run" |
| `Ctrl + Shift + Esc` | Task Manager |
| `Win + Space` | Switch input method / keyboard layout |
| `Alt + F4` | Close the active window |
| `Ctrl + Space` | Toggle IME |

Rules:

- Modifiers are `Ctrl`, `Alt`, `Shift`, and `Win`. **`Win` means the Windows logo key**; do not write `Windows`, `Super`, `Meta`, or the ⊞ symbol.
- Plain character keys are written as the character itself (`C`, `V`, `1`); case does not matter.
- Keys without a printable character must use a key name: `Space`, `Enter`, `Tab`, `Esc`, `Backspace`, `Delete`, `Insert`, `Home`, `End`, `PageUp`, `PageDown`, `Up`, `Down`, `Left`, `Right`, `F1`–`F24`.
- Spaces around `+` are optional, so `Ctrl+C` equals `Ctrl + C`. Full-width `＋` and full-width spaces are normalised to half-width automatically.
- Key names must be ASCII; Chinese names such as "空格键" are not recognised.

> **`Win + Space` is a system-level hotkey and takes a different send path than ordinary combos such as `Win + D`**: a lone Win key is emitted key-by-key with a short gap, otherwise the key-down and key-up are merged into one instant and nothing happens. Older versions had no `Space` key name and failed silently; this is supported now.

Structure of `config/key.json`:

```json
{
  "version": "1.0",
  "keys": [
    { "keyId": 1, "keyName": "Key 1", "actionType": "组合快捷键", "param": "Win + Space", "icon": "⌨️" },
    { "keyId": 2, "keyName": "Key 2", "actionType": "组合快捷键", "param": "Ctrl + C", "icon": "📋" }
  ]
}
```

`actionType` is one of `组合快捷键`, `启动应用`, `发送文本`, `多媒体控制`. For `启动应用` the `param` is a program or script path; for `多媒体控制` a `param` containing "静音"/"mute" means mute, anything else means play/pause.

Types accepted by `启动应用`:

| Suffix | How it is started |
| --- | --- |
| `.exe` | `QProcess::startDetached`, working directory set to the program's folder |
| `.lnk` | opened through ShellExecute, keeping embedded arguments and working directory |
| `.ps1` | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File`, so the machine execution policy does not need changing |
| `.bat` / `.cmd` | `cmd.exe /c` |

For scripts the working directory is set to the script's folder, so relative paths inside the script work. Paths containing spaces need no quoting — the script path and its arguments are passed to the interpreter separately.

## Using the UI

- Click an application card to view or edit its name, path, fallback icon character, and color; it can also be launched immediately.
- **Add more** opens a scanner for application shortcuts/executables and a form for manual entries.
- **Remote Smart** adds command-oriented modules without a local path. Add a path later if the command should launch a local program.
- **Export configuration** writes `share_bundle.zip` into the `config` folder by default. It contains the current `config/config.json` and `resources/<character>.png` images.
- Use the tray icon to restore the window. Closing the window leaves the process running in the tray.

## Security notes

- MQTT host, username, and password are saved through `QSettings`, which is not a secure credential vault. Use a trusted Windows account and secure the broker.
- The MQTT connection uses the configured host and port with no TLS settings exposed by the UI. Use an isolated trusted network, or extend the client to configure encrypted MQTT.
- Any publisher authorized to write to `home/esp32` can request a configured local application launch. Restrict broker ACLs and do not use untrusted command sources.
- `home/feishu` can trigger irreversible actions such as restart and shutdown. Configure the allowed Feishu sender `open_id` (stored in `QSettings` under `MQTT/allowedSender`; empty disables the check) and enforce sender authentication in the relay. The check in this application is defence in depth, not a substitute for the relay.
- Power actions enforce a 60 second minimum countdown, so a mistaken command can be undone with `power.cancel` before it fires.
- The `config` folder (`config.json`, `key.json`, `share_bundle.zip`) may expose local application paths and module identifiers; treat its files as sensitive.

## Project layout

```text
src/
  main.cpp                         Application setup and QML context objects
  models/ModuleModel.*             config/config.json-backed module list model
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
