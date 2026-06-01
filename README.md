<div align="center">

# ClawdSticker 🐾

**A desk-side companion whose face reacts to Claude Code in real time.**

It sits next to your keyboard as a tiny AMOLED creature. When Claude Code is
thinking, working, waiting for your confirmation, done, or has hit an error, the
face changes expression — and chirps on the moments that need you. No more
babysitting the terminal.

English · [中文](#中文)

[**⬇️ Download the macOS app**](https://github.com/orienthong0304/Clawdmeter/releases/latest) ·
[Recommended hardware](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)

<sub>Fork of [HermannBjorgvin/Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter) — the
upstream project is a Claude Code **usage** dashboard; this fork turns it into a
Claude Code **state-aware expression companion** (usage display is kept as a secondary screen).</sub>

</div>

---

## What it is

```
┌──────────────┐   hooks fire     ┌──────────────────┐   Bluetooth LE     ┌───────────────┐
│  Claude Code  │ ───────────────> │  ClawdSticker app │ ─────────────────> │   the device  │
│ (your Mac)    │   run a command  │   (macOS, menubar) │  write state char │  (ESP32-S3)    │
└──────────────┘                  └──────────────────┘                    └───────────────┘
                                                                          face animates + chirps
```

Three pieces:

1. **Claude Code hooks** — configured in `~/.claude/settings.json`, they run a
   tiny CLI on each Claude Code event (prompt submitted, tool about to run,
   waiting for approval, turn finished, error…).
2. **ClawdSticker** — a macOS menu-bar app. It owns the Bluetooth link to the
   device and the local socket the hooks talk to, maps events → expressions, and
   writes the state over BLE. It also has a token-usage page.
3. **The device firmware** — an ESP32-S3 board running an all-vector LVGL face
   engine (no sprite sheets) with **20 expression states** and cue tones.

> **Why Bluetooth, not Wi-Fi?** Corporate/home Wi-Fi often has AP isolation that
> blocks device-to-device packets, and you can't always touch the router. BLE is
> point-to-point, needs no provisioning, and just works in the only place that
> matters — your Mac sitting on the same desk.

## Expressions

The face is drawn from LVGL vector primitives (rounded-rect eyes, arcs, glow) and
tweened with `lv_anim`. Each state has a theme color, glow, and idle micro-motion.
The seven core states:

| State | Meaning | Looks like |
|---|---|---|
| `idle` | standing by | calm eyes, slow blink, faint cool-white glow |
| `thinking` | model reasoning | eyes glance side-to-side, brows, bouncing `…` dots (cyan) |
| `working` | running a tool | focused squint, sweeping progress ring (blue) |
| `waiting` | needs your confirmation | wide eyes, talking mouth, the face nudges for attention + **chirp** (amber) |
| `done` | turn finished | happy `∩∩` eyes, grin, sparkles + **success chime** (green) |
| `error` | something failed | `╳╳` eyes, droopy frown + **low error tone** (red) |
| `speaking` | (reserved for future TTS) | mouth flapping (purple) |

The firmware actually ships **20 states** (the seven above plus `listening`,
`searching`, `browsing`, `danger`, `permission`, `denied`, `bored`, `authok`,
`subdone`, `rate`, `compacting`, `touched`, `dizzy`, `sleep`) for finer-grained
hook mapping. See [`docs/UI_SPEC.md`](docs/UI_SPEC.md) and the live web preview in
[`docs/UI_PREVIEW.html`](docs/UI_PREVIEW.html).

## Hardware

**Recommended board — buy this one:**
[**Waveshare ESP32-S3-Touch-AMOLED-1.8**](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)

ESP32-S3R8 (8 MB PSRAM, 16 MB flash), 1.8" **368×448** AMOLED (SH8601, QSPI),
FT3168 touch, AXP2101 PMU, QMI8658 IMU, ES8311 codec + onboard speaker, USB-C.
This is the locked desk-buddy target — firmware, drivers and BLE plumbing are all
tuned for it.

You also need:

- a USB-C cable (flashing + charging)
- *(optional)* a 3.7 V Li-Po battery with an MX1.25 2-pin connector, to go cordless

> The upstream Waveshare AMOLED-2.16 board still builds (`waveshare_amoled_216`)
> but isn't a desk-buddy target. Porting to other boards: see
> [`docs/porting/`](docs/porting/).

## Quick start (macOS · Apple Silicon)

### 1 · Flash the firmware

Install [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
(`brew install platformio`), plug in the board, then:

```bash
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101
```

On boot the device shows the `idle` face and starts advertising over BLE as
**"Claude Controller"**.

### 2 · Install the ClawdSticker app

1. **[Download the latest `.dmg`](https://github.com/orienthong0304/Clawdmeter/releases/latest)**,
   open it, and drag **ClawdSticker** into Applications.
2. The build is **unsigned** (no Apple notarization), so clear the download
   quarantine once:
   ```bash
   xattr -dr com.apple.quarantine "/Applications/ClawdSticker.app"
   ```
   *(Or right-click the app → Open; on macOS 15+ use System Settings → Privacy &
   Security → “Open Anyway”.)*
3. Launch it. A 🐾 appears in the menu bar. Grant the Bluetooth permission when
   prompted; it connects to the device automatically.

### 3 · Wire up Claude Code hooks

One command sets up the Mac side — it makes a venv for the `deskbuddy` CLI and
merges the hooks into `~/.claude/settings.json` (idempotent, backs up first, and
leaves any other hooks you have untouched):

```bash
./setup-mac.sh
```

Now use Claude Code anywhere — the face follows along. (Hooks call `deskbuddy`,
which forwards each event to the app over a local socket; if the app/device is
offline it silently does nothing, so it can never block Claude Code.)

<details>
<summary>Manual hook config — what the script writes</summary>

Add this to `~/.claude/settings.json`, replacing `<REPO>` with the absolute path
to your clone:

```jsonc
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "command",
      "command": "<REPO>/bridge/deskbuddy event prompt-submit", "async": true }] }],
    "PreToolUse": [
      { "matcher": "Edit|Write",        "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event edit",   "async": true }] },
      { "matcher": "Read|Grep|Glob",    "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event search", "async": true }] },
      { "matcher": "WebFetch|WebSearch","hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event browse", "async": true }] },
      { "matcher": "Bash",              "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event bash",   "async": true }] }
    ],
    "PostToolUse":        [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event post-tool",    "async": true }] }],
    "PostToolUseFailure": [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event tool-failure", "async": true }] }],
    "Notification": [
      { "matcher": "idle_prompt",  "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event idle-prompt", "async": true }] },
      { "matcher": "auth_success", "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event auth-ok",     "async": true }] }
    ],
    "Stop":        [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event stop",         "async": true }] }],
    "StopFailure": [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event stop-failure", "async": true }] }]
  }
}
```

Event names were verified against Claude Code v2.1.159; they shift between
versions, so re-check against the current hooks docs if something doesn't fire.
</details>

## Modes & usage page

ClawdSticker has two tabs and two modes:

- **表情 / Expression** — the live face mirror + a grid to drive states by hand.
- **余量 / Usage** — Claude Code token usage (5h / 7d % bars + reset countdowns),
  fetched host-side on an always-on loop, so it works even when the device is
  offline; when connected it's piggybacked to the device's usage screen too.
- **Auto mode** (跟随 Claude Code) — hook events drive the face; a manual click
  wins for a few seconds over equal/lower-priority events.
- **Manual mode** (手动) — hooks are ignored (silently); only the grid drives it.

Closing the window just hides it; the menu-bar 🐾 re-shows it or quits.

## Build from source

<details>
<summary>Firmware</summary>

```bash
pio run -d firmware -e waveshare_amoled_18                 # build
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101
```

UI iteration: the firmware has a `screenshot` serial command;
`./screenshot.sh out.png` dumps the live LVGL framebuffer as a PNG.
See [`CLAUDE.md`](CLAUDE.md) for build gotchas (OPI PSRAM, LVGL-9 font patching,
the pioarduino platform pin).
</details>

<details>
<summary>The macOS app</summary>

```bash
# Run from source
python3 -m venv desktop/.venv
desktop/.venv/bin/pip install -r desktop/requirements.txt
PYTHONPATH=desktop desktop/.venv/bin/python -m deskbuddy_app

# Build the .app locally (unsigned)
desktop/packaging/build.sh        # → desktop/dist/ClawdSticker.app
```

Releases are built by [`.github/workflows/release-macos.yml`](.github/workflows/release-macos.yml):
pushing a `v*` tag runs PyInstaller, packs a `.dmg`, and publishes it. The
workflow ships **unsigned** (no Apple Developer Program needed). To add real
signing + notarization later, reintroduce the `codesign` / `notarytool` /
`stapler` steps — `desktop/packaging/entitlements.plist` is already in place for it.
</details>

## Repo layout

```
firmware/        ESP32-S3 firmware (HAL + per-board drivers + LVGL face engine + NimBLE)
  src/face.{h,cpp}    the 20-state vector face
  src/boards/         waveshare_amoled_18 (target) · waveshare_amoled_216 (upstream)
desktop/         ClawdSticker — pywebview menu-bar app (BLE owner + hook socket + usage)
  packaging/          PyInstaller spec, entitlements, icon, local build script
bridge/          deskbuddy CLI (hooks → local socket → app)
docs/            PROJECT_BRIEF · UI_SPEC · HOOKS_SPEC · UI_PREVIEW.html · porting/
.github/         release workflow
```

## Roadmap

Dangerous-command warnings · TTS voice playback · device settings (brightness /
flip / idle) over a new GATT characteristic · Wi-Fi provisioning · multi-session
display. See [`docs/PROJECT_BRIEF.md`](docs/PROJECT_BRIEF.md).

## Credits & licensing

- **Vector face engine** — original to this project (no third-party sprite sheets).
- **Clawd pixel-art splash** — by [@amaanbuilds](https://x.com/amaanbuilds) via
  [claudepix.vercel.app](https://claudepix.vercel.app); the firmware still uses
  these for its secondary splash screen.
- **Lucide** icons ([lucide.dev](https://lucide.dev), MIT) — Bluetooth/battery glyphs.
- **Noto Sans CJK** (SIL OFL 1.1) — Chinese text on-device.
- **Anthropic brand fonts** (Tiempos Text, Styrene B) — see the warning below.

> ⚠️ **Licensing gray area.** This repo bundles the **proprietary Anthropic brand
> fonts** (Styrene B, Tiempos Text) used without permission for status/title text,
> and **copyrighted Clawd** pixel-art assets. The code itself is non-proprietary,
> but because of those bundled assets it is **not** released under an open
> license. If you fork or copy, be aware you are responsible for those assets.
> **You have been warned.** (Inherited from upstream Clawdmeter.)

---
<a name="中文"></a>

# ClawdSticker 🐾（中文）

**一个放在桌上的小硬件:它的卡通脸会实时跟随 Claude Code 的状态切换表情。**

当 Claude Code 在思考、干活、等你确认、完成或报错时,屏幕上的脸会换表情,关键
时刻还会响提示音 —— 你不用再盯着终端。

[**⬇️ 下载 macOS 应用**](https://github.com/orienthong0304/Clawdmeter/releases/latest) ·
[推荐购买的开发板](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)

> Fork 自 [HermannBjorgvin/Clawdmeter](https://github.com/HermannBjorgvin/Clawdmeter)。
> 上游是 Claude Code **用量**仪表盘,本 fork 把它改造成 Claude Code **状态感知 +
> 表情反馈**的桌面伴侣(用量显示作为次要页面保留)。

## 这是什么

```
┌──────────────┐   hooks 触发     ┌──────────────────┐   蓝牙 BLE         ┌───────────────┐
│  Claude Code  │ ───────────────> │  ClawdSticker 应用 │ ─────────────────> │    开发板      │
│  (你的 Mac)   │   执行命令        │  (macOS 菜单栏)    │   写状态特征值       │  (ESP32-S3)    │
└──────────────┘                  └──────────────────┘                    └───────────────┘
                                                                          屏幕表情动画 + 提示音
```

三个部分:

1. **Claude Code hooks** — 在 `~/.claude/settings.json` 配置,Claude Code 每发生
   一个事件(提交输入、即将调工具、等待确认、一轮结束、报错…)就调用一个小 CLI。
2. **ClawdSticker** — 一个 macOS 菜单栏应用。它独占与开发板的蓝牙连接、以及 hooks
   说话用的本地 socket,把事件映射成表情并经 BLE 写给板子。它还有一个 token 用量页。
3. **开发板固件** — ESP32-S3,跑全矢量的 LVGL 表情引擎(不用序列帧),**20 个表情
   状态** + 提示音。

> **为什么用蓝牙而不是 WiFi?** 公司/家庭 WiFi 常有 AP 隔离(设备间不能互通),又
> 不一定能改路由器。蓝牙点对点、免配网,在唯一重要的场景(Mac 就在同一张桌上)
> 插上即用。

## 表情

脸由 LVGL 基础矢量图形(圆角矩形眼睛、圆弧、光晕)+ `lv_anim` 补间绘制。每个状态
有主题色、光晕和待机微动作。七个核心状态:

| 状态 | 含义 | 长相 |
|---|---|---|
| `idle` | 待命 | 平静的眼睛、慢眨眼、很弱的冷白光晕 |
| `thinking` | 模型推理中 | 眼睛左右看、有眉毛、下方 3 个跳动的 `…`(青色) |
| `working` | 正在调用工具 | 专注眯眼、绕脸旋转的进度环(蓝色) |
| `waiting` | 等你确认 | 瞪大眼、一张一合的嘴、整张脸晃动吸引注意 + **提示音**(琥珀) |
| `done` | 一轮结束 | 笑眼 `∩∩`、咧嘴、星星闪烁 + **成功音**(绿色) |
| `error` | 出错 | 叉叉眼 `╳╳`、耷拉嘴角 + **低沉错误音**(红色) |
| `speaking` | (预留给未来 TTS) | 嘴快速开合(紫色) |

固件实际内置 **20 个状态**(上面七个,外加 `listening`、`searching`、`browsing`、
`danger`、`permission`、`denied`、`bored`、`authok`、`subdone`、`rate`、
`compacting`、`touched`、`dizzy`、`sleep`),便于更细粒度地映射 hook 事件。详见
[`docs/UI_SPEC.md`](docs/UI_SPEC.md),网页预览在 [`docs/UI_PREVIEW.html`](docs/UI_PREVIEW.html)。

## 硬件

**推荐开发板 —— 买这块:**
[**Waveshare ESP32-S3-Touch-AMOLED-1.8**](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.8)

ESP32-S3R8(8 MB PSRAM,16 MB Flash),1.8" **368×448** AMOLED(SH8601,QSPI),
FT3168 触摸,AXP2101 电源管理,QMI8658 IMU,ES8311 编解码 + 板载扬声器,USB-C。
这是锁定的桌面伴侣目标板 —— 固件、驱动、蓝牙都是为它调好的。

还需要:

- 一根 USB-C 线(烧录 + 充电)
- *(可选)* 一块带 MX1.25 2-pin 接头的 3.7V 锂电池,实现无线摆放

> 上游的 Waveshare AMOLED-2.16 板仍可编译(`waveshare_amoled_216`),但不是桌面
> 伴侣目标。移植到其它板:见 [`docs/porting/`](docs/porting/)。

## 快速开始(macOS · Apple Silicon）

### 1 · 烧录固件

装好 [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation/index.html)
(`brew install platformio`),插上板子:

```bash
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101
```

上电后屏幕显示 `idle` 脸,并以 **“Claude Controller”** 的名字开始 BLE 广播。

### 2 · 安装 ClawdSticker 应用

1. **[下载最新 `.dmg`](https://github.com/orienthong0304/Clawdmeter/releases/latest)**,
   打开后把 **ClawdSticker** 拖进「应用程序」。
2. 这是**未签名**构建(没做 Apple 公证),首次需清一次下载隔离标记:
   ```bash
   xattr -dr com.apple.quarantine "/Applications/ClawdSticker.app"
   ```
   *(或右键 App →「打开」;macOS 15+ 在 系统设置 → 隐私与安全性 →「仍要打开」。)*
3. 启动它,菜单栏出现 🐾。按提示授予蓝牙权限,它会自动连上开发板。

### 3 · 配置 Claude Code hooks

一条命令搞定主机端 —— 它会为 `deskbuddy` CLI 建一个 venv,并把 hooks **幂等合并**
进 `~/.claude/settings.json`(先备份、不动你已有的其它 hooks):

```bash
./setup-mac.sh
```

然后在任意目录用 Claude Code,脸就会跟着动。(hooks 调用 `deskbuddy`,经本地
socket 把事件转发给应用;应用/板子离线时**静默跳过**,绝不会卡住 Claude Code。)

<details>
<summary>手动配置 hooks —— 脚本写入的内容</summary>

把下面这段加进 `~/.claude/settings.json`,把 `<REPO>` 换成你克隆仓库的绝对路径:

```jsonc
{
  "hooks": {
    "UserPromptSubmit": [{ "hooks": [{ "type": "command",
      "command": "<REPO>/bridge/deskbuddy event prompt-submit", "async": true }] }],
    "PreToolUse": [
      { "matcher": "Edit|Write",        "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event edit",   "async": true }] },
      { "matcher": "Read|Grep|Glob",    "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event search", "async": true }] },
      { "matcher": "WebFetch|WebSearch","hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event browse", "async": true }] },
      { "matcher": "Bash",              "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event bash",   "async": true }] }
    ],
    "PostToolUse":        [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event post-tool",    "async": true }] }],
    "PostToolUseFailure": [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event tool-failure", "async": true }] }],
    "Notification": [
      { "matcher": "idle_prompt",  "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event idle-prompt", "async": true }] },
      { "matcher": "auth_success", "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event auth-ok",     "async": true }] }
    ],
    "Stop":        [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event stop",         "async": true }] }],
    "StopFailure": [{ "hooks": [{ "type": "command", "command": "<REPO>/bridge/deskbuddy event stop-failure", "async": true }] }]
  }
}
```

事件名已对照 Claude Code v2.1.159 核实过;不同版本可能变动,若不触发请对照当前官方
hooks 文档复核。
</details>

## 模式与用量页

ClawdSticker 有两个标签页、两种模式:

- **表情** — 实时脸的镜像 + 手动切换状态的网格。
- **余量** — Claude Code token 用量(5h / 7d 百分比条 + 重置倒计时),host 端常驻
  拉取,板子离线也能看;连上时同样会推到板子的用量屏。
- **跟随模式(auto)** — hook 事件驱动表情;手动点击会在几秒内压过同/低优先级事件。
- **手动模式(manual)** — 忽略 hooks(静默),只由网格驱动。

关窗只是隐藏;菜单栏 🐾 可重新唤出或退出。

## 从源码构建

<details>
<summary>固件</summary>

```bash
pio run -d firmware -e waveshare_amoled_18                 # 编译
pio run -d firmware -e waveshare_amoled_18 -t upload --upload-port /dev/cu.usbmodem101
```

调 UI:固件有 `screenshot` 串口命令,`./screenshot.sh out.png` 会把实时 LVGL
画面导出成 PNG。构建坑(OPI PSRAM、LVGL 9 字体打补丁、pioarduino 平台锁版本)见
[`CLAUDE.md`](CLAUDE.md)。
</details>

<details>
<summary>macOS 应用</summary>

```bash
# 源码运行
python3 -m venv desktop/.venv
desktop/.venv/bin/pip install -r desktop/requirements.txt
PYTHONPATH=desktop desktop/.venv/bin/python -m deskbuddy_app

# 本地打包 .app(未签名)
desktop/packaging/build.sh        # → desktop/dist/ClawdSticker.app
```

Release 由 [`.github/workflows/release-macos.yml`](.github/workflows/release-macos.yml)
构建:推送 `v*` tag 即触发 PyInstaller 打包 → 封 `.dmg` → 发布。流水线产出**未
签名**(无需 Apple 开发者计划)。将来要加签名+公证,把 `codesign` /
`notarytool` / `stapler` 步骤加回即可 —— `desktop/packaging/entitlements.plist`
已经备好。
</details>

## 仓库结构

```
firmware/        ESP32-S3 固件(HAL + 板级驱动 + LVGL 表情引擎 + NimBLE)
  src/face.{h,cpp}    20 状态矢量脸
  src/boards/         waveshare_amoled_18(目标板) · waveshare_amoled_216(上游)
desktop/         ClawdSticker — pywebview 菜单栏应用(BLE owner + hook socket + 用量)
  packaging/          PyInstaller spec、entitlements、图标、本地构建脚本
bridge/          deskbuddy CLI(hooks → 本地 socket → 应用)
docs/            PROJECT_BRIEF · UI_SPEC · HOOKS_SPEC · UI_PREVIEW.html · porting/
.github/         发布流水线
```

## 路线图

危险命令预警 · TTS 语音播报 · 设备设置(亮度 / 翻转 / 待机)走新的 GATT 特征值 ·
WiFi 配网 · 多会话显示。见 [`docs/PROJECT_BRIEF.md`](docs/PROJECT_BRIEF.md)。

## 致谢与许可

- **矢量表情引擎** — 本项目原创(不用第三方序列帧)。
- **Clawd 像素画 splash** — 出自 [@amaanbuilds](https://x.com/amaanbuilds) 的
  [claudepix.vercel.app](https://claudepix.vercel.app);固件的次要 splash 屏仍在用。
- **Lucide** 图标([lucide.dev](https://lucide.dev),MIT)— 蓝牙/电池图标。
- **Noto Sans CJK**(SIL OFL 1.1)— 设备上的中文显示。
- **Anthropic 品牌字体**(Tiempos Text、Styrene B)— 见下方警告。

> ⚠️ **版权灰色地带。** 本仓库打包了**未经授权使用的 Anthropic 专有品牌字体**
> (Styrene B、Tiempos Text,用于状态/标题文字)以及**有版权的 Clawd** 像素画
> 资产。代码本身非专有,但因为捆绑了这些资产,本仓库**不**以开源许可证发布。
> 若你 fork 或拷贝,请自行为这些资产负责。**已警告。**(沿袭自上游 Clawdmeter。)
</content>
