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

`path` **除 `.exe` / `.lnk` 外还接受脚本**（`.ps1` / `.bat` / `.cmd`），与「快捷键」里「启动应用」的路径规则一致：脚本会被显式交给 `powershell.exe -File` 或 `cmd.exe /c` 执行，工作目录自动设为脚本所在目录。详细约定见下文「脚本执行的约定」。

## MQTT 指令协议

连接成功后，客户端会订阅两个命令主题：

```text
home/esp32   设备/NFC 指令，旧格式 {"value":"K1"}
home/feishu  飞书命令，信封格式，严格模式
```

### 旧格式（`home/esp32`）

向此主题发布包含 `value` 字段的 JSON。其值会与模块的 `character` 精确匹配：

```json
{
  "value": "K1"
}
```

对于没有专用处理器的指令，程序会尝试寻找 `character` 与收到的 `value` 相同的模块并启动其路径。`REBOOT` 与 `VOLUME_MUTE` 已接到实际能力：前者按 60 秒倒计时重启，后者执行系统静音。

### 信封格式（`home/feishu`）

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

- `v`、`action`、`msg_id`、`ts` 为必填，`params` 可缺省为 `{}`；
- `ts` 超过 **5 分钟** 的命令会被拒收（`expired`），防止 PC 离线期间 Broker 排队的旧命令在重连后突然执行；
- 同一个 `msg_id` 只执行一次，最近 100 条记录内重复的消息回 `duplicate`；
- 该主题处于**严格模式**：只接受带 `action` 的信封报文，不做 `character` 兜底匹配。

支持的动作：

| action | params | 说明 |
| --- | --- | --- |
| `power.restart` | `{"delay": 60}` | 计划重启，`delay` 低于 60 秒会被抬到 60 |
| `power.shutdown` | `{"delay": 60}` | 计划关机，同上 |
| `power.cancel` | `{}` | 取消已计划的关机/重启（`shutdown /a`） |
| `power.lock` | `{}` | 锁定工作站 |
| `power.sleep` | `{}` | 进入睡眠 |
| `app.launch` | `{"character": "N1"}` | 启动 `config.json` 中该 `character` 对应程序；**报文不含路径** |
| `shortcut.run` | `{"key": "K3"}` | 执行 `key.json` 中该编号的快捷键 |
| `media.control` | `{"op": "mute"}` | `mute` / `play_pause` / `volume_up` / `volume_down` |
| `clipboard.set` | `{"text": "..."}` | 写入剪贴板 |
| `notify.show` | `{"text": "...", "level": "info"}` | 弹出托盘通知 |
| `unknown` | `{"raw": "..."}` | 中继未识别命令时上报，只记日志，不执行 |

### 回执与能力清单

执行结果发布到 `home/feishu/ack`（retained，QoS 1），中继订阅后转回飞书：

```json
{ "v": 1, "msg_id": "om_xxx", "action": "power.restart", "status": "ok", "detail": "已计划 60 秒后重启", "ts": 1761726971 }
```

`status` 取值 `ok` / `denied` / `expired` / `duplicate` / `failed` / `unknown`。

连接成功后还会向 `home/feishu/caps` 发布一份 retained 能力清单，中继据此决定转发哪些动作，新增动作时中继无需改代码：

```json
{ "v": 1, "actions": ["power.restart", "..."], "min_power_delay": 60 }
```

> 命令主题**不得使用 retained 发布**，否则 PC 每次上线都会立刻收到一条历史命令并执行。

### 命令表配置（`config/feishu/commands.json`）

飞书命令词的映射表由 PC 维护，保存在 exe 上一级目录的 `config/feishu/commands.json`，首次启动自动生成一份可直接使用的默认表。改完保存即热重载，不需要重启程序。

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

| 字段 | 说明 |
| --- | --- |
| `schema` | 配置格式版本，当前固定为 1 |
| `minPowerDelay` | 电源类动作倒计时下限。**PC 侧硬下限 30 秒**，配置只能上调，低于 30 一律按 30 处理 |
| `allowedSenders` | 允许发送命令的飞书 `open_id`，为空表示不校验 |
| `unknownReply` | 未识别命令的回显模板，`{raw}` 替换为原文 |
| `commands` | 命令条目数组，**按顺序匹配，首个命中即停止**，优先级由排列顺序表达 |

每条命令的 `match.type` 支持三种：

| type | 语义 | 示例 |
| --- | --- | --- |
| `exact` | 与 `patterns` 中任一项完全相等 | 「重启」 |
| `prefix` | 以 `pattern` 开头，剩余部分写入 `captureAs` | 「启动 N1」→ `character=N1` |
| `regex` | 正则匹配，命名捕获组直接并入 `params` | 「音量调到 ...」 |

文本归一化只做去首尾空白、合并连续空白、全角转半角、ASCII 小写，**不做语义理解**。

PC 连接成功后会把这整份命令表以 retained 方式发布到 `home/feishu/config`，Armbian 订阅后据此完成「文本 → action」映射并落盘缓存，因此 PC 未启动时中继仍能工作。配置文件变化后会自动重新发布。

**容错**：配置 JSON 写坏时 PC 沿用上一次有效的命令表并打印告警，不会让程序直接失去命令能力；单条命令非法（`id` 重复、`action` 不在白名单、`patterns` 为空）只跳过该条，其余照常生效。

### 指令 → 功能映射表（`features`）

中文到指令的映射在 Armbian 侧完成，PC 侧只负责「拿到指令名 → 执行什么功能」。这段映射写在同一个配置文件里：

```json
"features": {
  "restart": { "type": "builtin", "target": "power.restart", "params": { "delay": 60 } },
  "deploy":  { "type": "script",  "target": "scripts/deploy.ps1", "params": {} }
}
```

| type | target | 含义 |
| --- | --- | --- |
| `builtin` | 动作名（如 `power.restart`） | 交给内置能力执行，动作名必须在内置动作表内 |
| `script` | 相对 `config` 目录的脚本路径 | 运行外部脚本，支持 `.ps1` / `.bat` / `.cmd` / `.exe` |
| `action` | 动作名 | 与 `builtin` 等价，保留兼容 armbian 直接下发 action 的报文 |

要点：

- **未在 `features` 中出现的指令沿用内置默认表**（restart / shutdown / cancel / lock / sleep / mute / launch / notify），配置文件缺失也照常可用；
- 脚本类功能的参数以 `--名字=值` 追加到命令行，**不经过 shell 拼接**，命令词里的引号或空格不会被解释成额外命令；
- 脚本扩展名有白名单限制，不放开 `.dll` / `.msi` 之类，避免配置被改坏后变成任意文件执行入口；
- 新功能扩展方式：把脚本放到 `config/feishu/scripts/`，在 `features` 里加一条，**不需要改代码、不需要重新编译、不需要重启程序**（保存即热重载）。

#### 脚本路径怎么填

`target` **两种写法都支持**，不用写死绝对路径也可以：

| 写法 | 示例 | 解析结果 |
| --- | --- | --- |
| 相对 `config` 目录 | `"scripts/deploy.ps1"` | `config/scripts/deploy.ps1` |
| 相对 `config/feishu` 目录 | `"scripts/deploy.ps1"`（放在 feishu 下时） | `config/feishu/scripts/deploy.ps1` |
| 绝对路径 | `"D:/Tools/deploy.ps1"` | 原样使用 |

实际规则很简单：**相对路径以 `config` 目录为基准**（见 `FeatureMap::resolveScriptPath`），所以 `"scripts/deploy.ps1"` 对应 `config/scripts/deploy.ps1`；只有想把脚本放在 `config/feishu/` 里时，才写成 `"feishu/scripts/deploy.ps1"`。目标文件不存在不会在加载时报错，只在执行时返回 `脚本不存在: <解析后的绝对路径>`——排查路径问题时看这条回执最快。

#### 完整可抄示例

`config/feishu/commands.json`：

```json
{
  "schema": 1,
  "features": {
    "cleanup": {
      "type": "script",
      "target": "scripts/cleanup.ps1",
      "params": { "days": 30 }
    },
    "backup": {
      "type": "script",
      "target": "D:/Tools/backup.bat"
    }
  }
}
```

`config/scripts/cleanup.ps1`：

```powershell
param([int]$days = 7)
# 参数以 --days=30 形式传入：脚本里用 param() 或直接读 $args 都行
Write-Output "清理 $days 天前的临时文件"
Get-ChildItem $env:TEMP -Recurse -File |
    Where-Object { $_.LastWriteTime -lt (Get-Date).AddDays(-$days) } |
    Remove-Item -Force -ErrorAction SilentlyContinue
exit 0
```

触发方式（两种都行）：

```powershell
# A. 旧式 value 分支，指令名直接命中 features 的键
mosquitto_pub -h <broker> -t home/esp32 -m '{"value":"cleanup"}'

# B. 飞书信封分支
mosquitto_pub -h <broker> -t home/feishu -m '{"v":1,"action":"shortcut.run","params":{"key":"K1"},"msg_id":"om_x","ts":1761726970}'
```

注意 A 这条只对 `home/esp32` 生效（`home/feishu` 是严格模式，见上文）。要让脚本也能从飞书触发，需要在 `commands` 里加一条把中文映射到某个 `action`，再在 `features` 里把该 `action` 指到脚本——但 `action` 必须是白名单里的名字，脚本类功能目前只能通过 `value` 分支或 `shortcut.run` 间接到达。

#### 脚本执行的约定

- 后缀白名单：`.ps1` / `.bat` / `.cmd` / `.exe`，其余后缀在加载时就被拒绝并记一条错误；
- `.ps1` 用 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` 运行，所以不用改系统执行策略；
- 工作目录自动设为脚本所在目录，脚本里可以直接用相对路径；
- 参数以 `--名字=值` 追加，不经过 shell，带空格或引号的命令词不会被拆成额外命令；
- 超时 **30 秒**，超时会被 kill 并回 `脚本执行超时（30 秒）`；退出码非 0 视为失败，stderr 会被截取前 200 字符放进回执；
- 扩展名是 `.exe` 时直接执行该程序，参数同样以 `--名字=值` 传入。

## 快捷键配置（`config/key.json`）

“快捷键”对话框为 6 键宏键盘的 K1–K6 各配置一个动作：组合快捷键、启动应用、发送文本、多媒体控制，保存到 `config/key.json`。MQTT 的 `shortcut.run` 指令通过 `{"key":"K3"}` 执行同一份配置。

组合快捷键的 `param` 用 `+` 连接的**英文键名**表示：

| 写法 | 含义 |
| --- | --- |
| `Ctrl + C` | 复制 |
| `Win + D` | 显示桌面 |
| `Win + R` | 打开“运行” |
| `Ctrl + Shift + Esc` | 任务管理器 |
| `Win + Space` | 切换输入法 / 键盘布局 |
| `Alt + F4` | 关闭当前窗口 |
| `Ctrl + Space` | 中英文切换 |

写法规则：

- 修饰键固定写 `Ctrl` / `Alt` / `Shift` / `Win`；**Win 指 Windows 徽标键**，不要写成 `Windows`、`Super`、`Meta` 或 ⊞ 符号。
- 普通字符键直接写该字符（`C`、`V`、`1`），不区分大小写。
- 空格、方向键等功能键没有可打印字符，必须写键名：`Space`、`Enter`、`Tab`、`Esc`、`Backspace`、`Delete`、`Insert`、`Home`、`End`、`PageUp`、`PageDown`、`Up`、`Down`、`Left`、`Right`、`F1`–`F24`。
- `+` 两侧空格可有可无，`Ctrl+C` 与 `Ctrl + C` 等价；全角的 `＋` 和全角空格会自动转成半角。
- 键名必须是 ASCII 半角字符，中文“空格键”“回车键”不会被识别。

> **`Win + Space` 是系统级热键，它与 `Win + D` 这类普通组合键走不同的发送路径**：单独一个 Win 键会分段发送并留出间隔，否则按下与抬起在同一瞬间被合并、等于没按。旧版本没有 `Space` 键名、解析失败还会静默不生效，现已支持。

`config/key.json` 结构：

```json
{
  "version": "1.0",
  "keys": [
    { "keyId": 1, "keyName": "按键 1", "actionType": "组合快捷键", "param": "Win + Space", "icon": "⌨️" },
    { "keyId": 2, "keyName": "按键 2", "actionType": "组合快捷键", "param": "Ctrl + C", "icon": "📋" }
  ]
}
```

`actionType` 取值 `组合快捷键` / `启动应用` / `发送文本` / `多媒体控制`；`启动应用` 的 `param` 是程序或脚本路径，`多媒体控制` 的 `param` 含“静音”时按静音，否则按播放/暂停。

「启动应用」的 `param` 支持以下类型：

| 后缀 | 实际执行方式 |
| --- | --- |
| `.exe` | `QProcess::startDetached` 直接启动，工作目录为程序所在目录 |
| `.lnk` | 经 ShellExecute 打开，保留快捷方式内的参数与工作目录 |
| `.ps1` | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File` 执行，不用改系统执行策略 |
| `.bat` / `.cmd` | `cmd.exe /c` 执行 |

脚本类的工作目录自动设为脚本所在目录，脚本里可以直接用相对路径。路径含空格也无需加引号——脚本路径与参数是分开传给解释器的，不会被拆断。

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
- `home/feishu` 可触发重启、关机等不可逆动作。请在设置中填写允许的飞书发送者 `open_id`（保存在 `QSettings` 的 `MQTT/allowedSender`，留空则不校验），并在中继侧完成发送者鉴权；上位机侧的校验只是纵深防御，不能替代中继。
- 电源类动作强制 60 秒倒计时下限，误发时可在倒计时内发送 `power.cancel` 撤销。
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
