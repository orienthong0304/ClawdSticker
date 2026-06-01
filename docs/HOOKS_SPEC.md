# HOOKS_SPEC — Claude Code Hooks 状态映射规范

> 本文件定义 Claude Code 的事件如何映射到桌面伴侣的 7 个状态，
> 以及危险操作预警的规则。供 Claude Code 实现 Mac 端 bridge 与
> 生成 hooks 配置时参考。

## 1. 工作原理

Claude Code 支持 hooks：在特定事件发生时执行 shell 命令。
我们利用这一点，在事件触发时调用 Mac 端 bridge 的 CLI，
由 bridge 通过蓝牙 BLE 把状态写给开发板。

```
Claude Code 事件 → 执行 hook 命令 → 调用 deskbuddy CLI → bridge 经 BLE 写状态 → 板子换表情
```

> 注意：hooks 只能执行 shell 命令，不能直接调用 MCP 工具。
> 所以 bridge 必须提供一个命令行接口（CLI），供 hook 调用。

## 2. 事件 → 状态映射

> Claude Code 的具体 hook 事件名称可能随版本变化，
> 请 Claude Code 以**当前官方文档**为准核对事件名，下表是设计意图。

| Claude Code 事件（设计意图） | 映射状态 | 说明 |
|------|------|------|
| 会话开始 / 空闲 | `idle` | 待命 |
| 用户提交输入后 | `thinking` | 模型开始推理 |
| 即将调用工具（PreToolUse） | `working` | 开始干活；如命中危险规则则改判 `waiting`/`error` |
| 工具调用结束（PostToolUse） | `working` | 维持工作态（可选：单步完成微反馈）|
| 需要用户确认 / 通知（Notification） | `waiting` | 等待确认，**触发提示音** |
| 一轮任务结束（Stop） | `done` | 完成，**触发提示音**；数秒后回落 `idle` |
| 工具执行失败 / 报错 | `error` | 出错，**触发提示音** |

补充行为：
- `done` 状态显示数秒后，bridge 应自动让板子回到 `idle`。
- `error` 状态显示数秒后，可回到 `idle`（或保持到下一次事件）。
- 状态切换以"最后一次事件"为准。

## 3. 危险操作预警（第一步不做，留作后续）

> 第一步**不实现**危险操作预警，先把基础状态感知做扎实。
> 此处仅记录设计意图，供第二步参考。

后续版本计划：在 `PreToolUse` 事件里检查命令内容，命中危险模式
（如 `rm -rf`、`git push --force`、`DROP TABLE` 等）时切到 `error`/`waiting`
状态并强提醒。届时建议把危险规则写成可配置的列表，方便使用者自行增删。

第一步里，`PreToolUse` 事件统一映射为 `working` 即可，不做命令内容判断。

## 4. Mac 端 bridge 的 CLI 设计建议

bridge 是一个 Python 程序，至少提供这样一个命令行接口：

```
deskbuddy set-state <state>            # 直接设置状态
deskbuddy event <event-name>           # 传入 Claude Code 事件，由 bridge
                                       # 内部决定映射到哪个状态
```

- hook 里优先调用 `deskbuddy event ...` 的形式，把映射逻辑收敛在 bridge 里。
- bridge 作为 BLE 中心设备连接开发板。开发板的 BLE 地址 / 设备名
  通过配置文件提供，或由 bridge 按设备名自动扫描发现。
- 关于连接保持：每次 hook 都新建 BLE 连接会有连接延迟。建议 bridge
  以一个常驻后台进程维持 BLE 连接，CLI 只是把指令转发给该常驻进程
  （例如通过本地 socket / 命名管道）。常驻进程的实现方式由 Claude Code
  按 Clawdmeter 原有 daemon 的做法决定。
- bridge 推送失败（板子未连接 / 关机）时应静默失败，不能阻塞 Claude Code。

## 5. hooks 配置示例（结构示意）

最终要生成一份可直接放进 `~/.claude/settings.json` 的 hooks 配置。
结构大致如下（**事件名以当前 Claude Code 官方文档为准**）：

```jsonc
{
  "hooks": {
    "<提交输入事件>":  [{ "hooks": [{ "type": "command",
        "command": "deskbuddy event prompt-submit" }]}],
    "<调用工具前事件>": [{ "hooks": [{ "type": "command",
        "command": "deskbuddy event pre-tool" }]}],
    "<需要确认事件>":  [{ "hooks": [{ "type": "command",
        "command": "deskbuddy event notification" }]}],
    "<任务结束事件>":  [{ "hooks": [{ "type": "command",
        "command": "deskbuddy event stop" }]}]
  }
}
```

> 请 Claude Code 核对当前版本 Claude Code 的 hooks 事件名与可用变量，
> 据此生成最终配置。

## 6. 验收

- 在终端正常使用 Claude Code 时，板子能跟随事件切换表情。
- 提交输入变 thinking、调工具变 working、需要确认变 waiting（响提示音）、
  任务结束变 done（响提示音）、报错变 error（响提示音）。
- 蓝牙未连接 / 板子关机时，Claude Code 使用完全不受影响。
