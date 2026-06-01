# 桌面伴侣 Desk Buddy — 项目改造说明书（第一步 / MVP）

> 本文档供 Claude Code 阅读，用于理解项目背景、目标与改造范围。
> 这是一个基于 `HermannBjorgvin/Clawdmeter` fork 出来的项目，目标是把它从
> "Claude Code token 用量仪表盘" 改造成 "Claude Code 状态感知 + 表情反馈的桌面伴侣"。

---

## 1. 一句话目标

把开发板改造成一个放在桌上的小硬件：当 Claude Code 在运行时，
屏幕上一张卡通脸会根据 Claude Code 的实时状态切换表情动画，
关键时刻（需要确认、危险操作、任务完成、出错）给出明显的视觉 + 声音提醒。

## 2. 背景与动机

- 使用者是一名重度 Claude Code 用户，经常同时跑多个会话和长任务。
- 痛点：Claude Code 在终端里跑，使用者去做别的事时，不知道它现在
  是在思考、在干活、卡住了在等确认、还是已经完成 / 报错。
- 现有的 Clawdmeter 只显示 token 用量，不反映"运行状态"。
- 目标产物：一个**会用表情和声音表达 Claude Code 当前状态**的桌面摆件。

## 3. 硬件

- 开发板：**Waveshare ESP32-S3-Touch-AMOLED-1.8**
- 主控：ESP32-S3R8，Xtensa 双核 240MHz，8MB PSRAM，16MB Flash
- 屏幕：1.8" AMOLED，**368 × 448** 分辨率，驱动 IC **SH8601**（QSPI）
- 触控：FT3168（I2C）
- 其它板载：QMI8658 六轴 IMU、PCF85063 RTC、AXP2101 电源管理、
  ES8311 音频编解码 + 板载扬声器、麦克风、micro SD 卡槽
- 侧边按键：BOOT、PWR（可自定义）
- 供电：Type-C / 3.7V MX1.25 锂电池

> 重要：Clawdmeter 仓库里已存在 `firmware/src/boards/waveshare_amoled_18/`
> 这个 board 目录，对应的就是这块 1.8" 板（SH8601 + 368×448）。
> 硬件驱动层（屏幕、触摸、IMU、电源）应当**尽量复用，不要重写**。

## 4. 通信方案（第一步）

- **Mac ↔ 开发板：走蓝牙 BLE。**
- 沿用 Clawdmeter 原有的蓝牙方案：开发板作为 BLE 外设，
  Mac 端程序作为 BLE 中心设备连接它，通过一个自定义的
  BLE 特征值（characteristic）把"状态"写给板子。
- 选择蓝牙的原因：
  1. Clawdmeter 原本就是蓝牙方案，板子端 + Mac 端代码可直接复用，
     移植成本最低。
  2. 不依赖局域网。公司 / 家庭 WiFi 常有 AP 隔离（设备间不能互通），
     WiFi 方案可能直接不通，且通常无法自行修改路由器配置。
  3. 蓝牙点对点连接，不用管 WiFi 账号密码、不用配网，插上即用。
- 摆件依然是无线的（蓝牙也是无线），只需在 Mac 附近（同一房间内）即可。
- WiFi 方案留作后续：当未来需要"手机控制""多机共用""对接云端 Agent"
  等扩展能力时再考虑，第一步不做。

## 5. 系统架构

```
┌──────────────┐   hooks 触发     ┌─────────────────┐   蓝牙 BLE         ┌──────────────┐
│ Claude Code  │ ───────────────> │  Mac 端 bridge   │ ─────────────────> │  开发板固件    │
│ (在 Mac 终端) │   调用 CLI       │  (Python 脚本)   │   写状态特征值       │  (ESP32-S3)   │
└──────────────┘                  └─────────────────┘                    └──────────────┘
                                                                          屏幕显示表情动画
                                                                          + 提示音
```

三个部分：

1. **Claude Code hooks**：在 `~/.claude/settings.json` 配置，
   不同事件触发时执行命令，调用 Mac 端 bridge 的 CLI。
2. **Mac 端 bridge**（Python）：接收状态切换请求，作为 BLE 中心设备
   连接开发板，把状态写入板子的 BLE 特征值。第一步它是单向的
   （Claude Code → 板子）。
3. **开发板固件**（ESP32-S3 / C++）：作为 BLE 外设广播并接受连接，
   收到状态写入后驱动 LVGL 表情 UI。

## 6. 第一步（MVP）范围

### 必须做
- [ ] 固件：为这块 1.8" 板跑通 BLE 外设服务（广播 + 一个可写的状态特征值）
- [ ] 固件：实现 7 个状态的 LVGL 表情动画（见 `UI_SPEC.md`）
- [ ] 固件：收到状态写入时，平滑切换到对应表情动画
- [ ] 固件：关键状态（waiting / done / error）触发提示音（板载扬声器，简单蜂鸣/音效即可）
- [ ] Mac 端：一个 Python bridge，提供 CLI，作为 BLE 中心设备把状态写给板子
- [ ] Claude Code hooks 配置：把 Claude Code 事件映射到状态切换（见 `HOOKS_SPEC.md`）

### 第一步不做（留到后续）
- **危险操作预警** —— 第一步先做扎实的基础状态感知，预警留到第二步
- 语音播报（TTS）—— 第一步只做提示音，不做人声
- 麦克风 / 语音反向指令
- Dida365 滴答清单联动
- WiFi 通信 / 配网 —— 第一步用蓝牙；WiFi 留作后续扩展
- 多项目 / 多会话区分显示
- 触摸屏交互逻辑

## 7. 状态定义（7 个）

| 状态 | 含义 | 触发时机 |
|------|------|----------|
| `idle` | 待命 | Claude Code 空闲 / 会话开始 |
| `thinking` | 思考中 | 用户提交了输入，模型在推理 |
| `working` | 执行任务 | 正在调用工具（写文件、跑命令等） |
| `waiting` | 等待用户确认 | Claude Code 需要用户确认 / 授权 |
| `speaking` | 说话（第一步预留） | 第一步不触发，UI 先实现，留给第二步语音 |
| `done` | 任务完成 | 一轮任务结束 |
| `error` | 出错 | 命令失败 / 检测到危险操作 |

## 8. 设计原则（请 Claude Code 遵守）

- **复用优先**：Clawdmeter 的 HAL 分层（`firmware/src/hal/`）、
  `boards/waveshare_amoled_18/` 板级驱动、构建配置，尽量保留复用。
- **UI 层可替换**：Clawdmeter 把 UI 隔离在 `ui.cpp` / `splash.cpp` 等文件，
  表情系统应作为新的 UI 实现替换进去，不要动 HAL。
- **表情用矢量绘制，不用序列帧**：眼睛 / 眉毛 / 嘴用 LVGL 基础图形对象
  （圆角矩形、圆弧、圆形）+ 补间动画实现。不要用大图序列帧，
  以节省 Flash 和内存，也方便后续调整。
- **改动要小步、可验证**：先让 WiFi + 一个静态表情显示出来，
  再逐步加动画和状态切换。每一步都应能编译、能烧录验证。
- **不破坏原仓库的 board 移植框架**：如需新增配置，按 Clawdmeter
  既有的 per-board 目录约定来做。

## 9. 验收标准（第一步做完的样子）

1. 开发板上电后，屏幕显示 `idle` 表情（待命脸 + 慢眨眼），并开始 BLE 广播。
2. Mac 端 bridge 能扫描并连接到开发板。在 Mac 终端运行 bridge 的 CLI
   （如 `deskbuddy set-state working`），开发板屏幕在 1 秒内切换到对应表情。
3. 配好 hooks 后，在任意目录使用 Claude Code：
   - 提交输入 → 板子变 `thinking`
   - Claude 调用工具 → 变 `working`
   - Claude 需要确认 → 变 `waiting` 并响提示音
   - 任务完成 → 变 `done` 并响提示音
   - 命令报错 → 变 `error` 并响提示音
4. 7 个表情动画都能流畅播放，无明显卡顿或撕裂。
5. 蓝牙未连接 / 板子关机时，Claude Code 使用完全不受影响。

## 10. 配套文档

- `UI_SPEC.md` — 7 个状态的表情动画详细设计规范
- `HOOKS_SPEC.md` — Claude Code hooks 事件 → 状态映射，含危险操作预警规则
- 同时附带一个 HTML 表情预览原型文件，作为视觉参考（动画风格以此为准）

## 11. 给 Claude Code 的第一批任务建议

建议按这个顺序推进，每步都要能编译验证：

1. 先把 fork 的仓库结构摸清楚，重点看 `boards/waveshare_amoled_18/`、
   `hal/`、`ui.cpp`、`ble.cpp`、`platformio.ini`，以及 Clawdmeter 原有的
   BLE 实现和 Mac 端 daemon，向我汇报现状和改造计划。
2. 确认 Clawdmeter 原有的 BLE 服务能在这块板上跑起来，
   能被 Mac 扫描 / 连接到。
3. 把 BLE 上的数据格式改造成"状态字符串"（替换原本的 token 用量数据），
   板子收到后能在串口打印出来。
4. 用 LVGL 画出 1 个静态表情（先做 idle 的脸），确认屏幕显示正常。
5. 把 7 个表情和动画补全（参考 `UI_SPEC.md`）。
6. 打通"收到 BLE 状态写入 → 切换表情"。
7. 写 Mac 端 Python bridge + CLI（作为 BLE 中心设备连接板子）。
8. 配置并测试 Claude Code hooks。
9. 加 waiting / done / error 三个状态的提示音。
