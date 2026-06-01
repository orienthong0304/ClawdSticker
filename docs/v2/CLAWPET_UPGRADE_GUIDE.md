# ClawPet 升级开发指南 v2 — 更丰富的表情 + 更多状态 + 更多 Hook

> 给实现者(人或 Claude Code)的话:本文是**增量改造规格**。现有 BLE 桥架构、`notchi-hook.sh`、vibeusage、statusLine、plugins 全部保留,不要重写。只做三件事:(1) 扩充状态词表;(2) 扩充 hook → event → state 映射;(3) 把固件里的"眼睛"换成本文 §5 规定的程序化机器眼引擎。视觉基准以随附的 `clawpet-face.html` 为准——固件最终效果要尽量还原那个网页里的表情和过渡。

---

## 0. 现有架构(保持不变)

```
Claude Code hook (command 类型)
        │  调用 `deskbuddy event <e>`
        ▼
deskbuddy CLI  ──(Unix socket 一行文本)──▶  deskbuddy daemon
        │                                         │ EVENT_MAP: event → state
        │                                         ▼
        │                              BLE GATT write (STATE_CHAR …0005)
        ▼                                         │  状态字符串(ASCII)
   立即退出, 永不阻塞 Claude Code                  ▼
                                          固件: 收到状态串 → 渲染对应表情
```

不变量:
- 传输仍是 **BLE**,设备名 `Claude Controller`,状态写入特征 `4c41555a-4465-7669-6365-000000000005`,内容是 ASCII 状态字符串,`response=False`。
- hook 仍是 `command` 类型、`async:true`、`timeout:5`,失败静默(绝不阻塞 Claude Code)。
- 守护进程仍持有长连接、单一 `desired` 状态、断线自动重连、瞬态自动回落。
- `notchi-hook.sh` 与 vibeusage 的 hook 行**原样保留**,本次只改/加 `deskbuddy` 相关行。

要改的只有:**状态多了、事件多了、固件那只眼睛重画了**。

---

## 1. 状态词表(state vocabulary)

固件和守护进程都以这套 **canonical state id** 为准(小写、短、BLE 友好)。新增状态见标注。

| state id | 含义 | 类型 | 旧词表兼容 |
|---|---|---|---|
| `idle` | 待机/醒着、平静 | 常驻 | 既有 |
| `listening` | 在听(语音输入用,文本流可不触发) | 常驻 | 新增 |
| `thinking` | 思考/正在生成回复 | 常驻 | 既有 |
| `working` | 写代码/执行命令 | 常驻 | 既有 |
| `searching` | 读取/检索代码 | 常驻 | 新增 |
| `browsing` | 联网查资料 | 常驻 | 新增 |
| `danger` | 危险操作(rm -rf / force push 等) | **粘性瞬态** | 新增 |
| `permission` | 需要你批准权限 | 常驻(高优先) | = 旧 `waiting` |
| `denied` | 权限被拒、委屈 | 瞬态 | 新增 |
| `bored` | 长时间发呆等待 | 常驻 | 新增 |
| `authok` | 登录成功 | 瞬态 | 新增 |
| `done` | 任务完成、得意 | 瞬态 | 既有 |
| `subdone` | 子 agent 完成 | 瞬态 | 新增 |
| `error` | 工具/命令执行失败 | 瞬态 | 既有 |
| `rate` | 被限流 / API 错误、累了 | 瞬态 | 新增 |
| `compacting` | 压缩上下文/整理记忆 | 常驻 | 新增 |
| `touched` | 被摸头(电容屏) | 瞬态·板载 | 新增 |
| `dizzy` | 被摇晃(IMU) | 瞬态·板载 | 新增 |
| `sleep` | 会话结束、睡觉 | 常驻 | 新增 |
| `speaking` | 朗读/TTS(你原有) | 常驻 | 既有 |

**降级别名(固件/守护进程都要带)**:实现者若分阶段上线,未实现的状态按下表回落,保证永不黑屏。
`searching→working`, `browsing→working`, `bored→idle`, `subdone→done`, `authok→done`, `rate→error`, `listening→thinking`, `compacting→thinking`, `touched→done`, `dizzy→idle`, `waiting→permission`(旧词)。
`danger` **不允许降级**,必须实现。

---

## 2. Hook → event → state 映射

### 2.1 `~/.claude/settings.json` 的 hooks 改动(BLE/deskbuddy 风格)

把 `bridge/deskbuddy` 的可执行路径记为 `$DB`(你的是 `/Users/orienthong/Shuai/Clawdmeter/bridge/deskbuddy`)。

要点:
- **PreToolUse 拆成按工具的 matcher 组**,这样守护进程仍然"哑"(只做 event→state),路由放在 settings 里。把原来那条无 matcher 的 `deskbuddy event pre-tool` 替换掉。
- **危险检测用官方 `if` 字段**(权限规则语法),无需自己 grep。同一条 Bash 命中危险时会同时发 `danger` 和 `bash`,靠守护进程的**粘性优先级**(§2.2)让 `danger` 压住 `working`。
- `notchi-hook.sh` 行保持不动,只把 deskbuddy 行按下面替换/新增。

下面是**仅 deskbuddy 相关**的 hooks 片段(把它合并进你现有 hooks,与 notchi 行并存):

```jsonc
{
  "hooks": {
    "SessionStart":     [{ "hooks": [{ "type": "command", "command": "$DB event session-start", "async": true, "timeout": 5 }] }],
    "UserPromptSubmit": [{ "hooks": [{ "type": "command", "command": "$DB event prompt-submit", "async": true, "timeout": 5 }] }],

    "PreToolUse": [
      { "matcher": "Edit|Write",
        "hooks": [{ "type": "command", "command": "$DB event edit",   "async": true, "timeout": 5 }] },
      { "matcher": "Read|Grep|Glob",
        "hooks": [{ "type": "command", "command": "$DB event search", "async": true, "timeout": 5 }] },
      { "matcher": "WebFetch|WebSearch",
        "hooks": [{ "type": "command", "command": "$DB event browse", "async": true, "timeout": 5 }] },
      { "matcher": "Bash",
        "hooks": [
          { "type": "command", "if": "Bash(rm -rf *)",        "command": "$DB event danger", "async": true, "timeout": 5 },
          { "type": "command", "if": "Bash(git push --force*)","command": "$DB event danger", "async": true, "timeout": 5 },
          { "type": "command",                                 "command": "$DB event bash",   "async": true, "timeout": 5 }
        ] }
    ],

    "PostToolUse":        [{ "hooks": [{ "type": "command", "command": "$DB event post-tool",         "async": true, "timeout": 5 }] }],
    "PostToolUseFailure": [{ "hooks": [{ "type": "command", "command": "$DB event tool-failure",      "async": true, "timeout": 5 }] }],

    "PermissionRequest":  [{ "matcher": "*", "hooks": [{ "type": "command", "command": "$DB event permission-request", "async": true, "timeout": 5 }] }],
    "PermissionDenied":   [{ "hooks": [{ "type": "command", "command": "$DB event permission-denied", "async": true, "timeout": 5 }] }],

    "Notification": [
      { "matcher": "idle_prompt",   "hooks": [{ "type": "command", "command": "$DB event idle-prompt", "async": true, "timeout": 5 }] },
      { "matcher": "auth_success",  "hooks": [{ "type": "command", "command": "$DB event auth-ok",     "async": true, "timeout": 5 }] }
    ],

    "SubagentStop": [{ "hooks": [{ "type": "command", "command": "$DB event subagent-stop", "async": true, "timeout": 5 }] }],
    "Stop":         [{ "hooks": [{ "type": "command", "command": "$DB event stop",          "async": true, "timeout": 5 }] }],
    "StopFailure":  [{ "hooks": [{ "type": "command", "command": "$DB event stop-failure",  "async": true, "timeout": 5 }] }],
    "PreCompact":   [{ "matcher": "*", "hooks": [{ "type": "command", "command": "$DB event precompact", "async": true, "timeout": 5 }] }],
    "SessionEnd":   [{ "hooks": [{ "type": "command", "command": "$DB event session-end",   "async": true, "timeout": 5 }] }]
  }
}
```

> 说明:
> - 危险检测的 `if` 是官方权限规则语法,匹配 Bash 子命令(已剥离前置 `VAR=value`),所以 `FOO=1 rm -rf x` 也会命中。可按需再加规则行(每行一条,`if` 不支持 `&&`/`||`)。
> - `PermissionRequest` 与 `Notification·permission_prompt` 都在权限节点附近触发;这里用 `PermissionRequest`(你已有)即可,不必两个都接。
> - `MessageDisplay` 可选:它在助手"吐字"时触发,可用来驱动 `thinking`,但很频繁;默认不接,靠 `prompt-submit→thinking` 已够。

### 2.2 `bridge/deskbuddy` 守护进程改动

仅改三处:`STATES`、`EVENT_MAP`、瞬态/优先级逻辑。CLI、socket、BLE 重连等不动。

**STATES**(扩成完整词表):
```python
STATES = (
    "idle", "listening", "thinking", "working", "searching", "browsing",
    "danger", "permission", "denied", "bored", "authok", "done", "subdone",
    "error", "rate", "compacting", "touched", "dizzy", "sleep", "speaking",
)
```

**EVENT_MAP**(覆盖旧表;键 = settings.json 里传的 event 参数):
```python
EVENT_MAP = {
    "session-start":      "idle",
    "prompt-submit":      "thinking",
    "edit":               "working",
    "search":             "searching",
    "browse":             "browsing",
    "bash":               "working",
    "danger":             "danger",
    "post-tool":          "working",     # 维持工作态;也可改成 no-op
    "tool-failure":       "error",
    "permission-request": "permission",  # 旧映射是 waiting,现拆为 permission
    "permission-denied":  "denied",
    "idle-prompt":        "bored",
    "auth-ok":            "authok",
    "subagent-stop":      "subdone",     # 旧映射是 done
    "stop":               "done",
    "stop-failure":       "rate",        # 旧映射是 error;StopFailure 是 API 错误,非代码错
    "precompact":         "compacting",
    "session-end":        "sleep",
    # 兼容旧值
    "pre-tool":           "working",
    "notification":       "permission",
    "error":              "error",
    "idle":               "idle",
}
```

**瞬态 + 粘性优先级**(替换原来只针对 done/error 的回落逻辑):
```python
# 这些状态显示几秒后自动回到 idle
TRANSIENT = {"done", "subdone", "authok", "error", "rate", "denied", "danger", "touched", "dizzy"}
REVERT_SECONDS = 4.0
DANGER_STICKY_SECONDS = 3.0   # danger 期间忽略 working/searching 等"降级"刷新

# 优先级:数字大者压制小者(同一时刻该显示谁)
PRIORITY = {
    "danger": 100, "permission": 90, "denied": 70,
    "error": 60, "rate": 60, "done": 50, "subdone": 50, "authok": 50,
    "touched": 80, "dizzy": 80,           # 板载传感器瞬态,短暂高优先
    "compacting": 30, "browsing": 20, "searching": 20, "working": 20,
    "thinking": 15, "bored": 12, "listening": 12, "speaking": 18,
    "sleep": 10, "idle": 5,
}
```

`apply_state()` 的语义升级为:
1. 若当前处于 `danger` 粘性窗口内,且新状态优先级 < danger,则**忽略**该刷新(让危险脸顶住几秒)。
2. 否则采用新状态;若新状态 ∈ `TRANSIENT`,安排 `REVERT_SECONDS` 后回落 `idle`(`danger` 用 `DANGER_STICKY_SECONDS`)。
3. 回落定时器在每次新状态到来时取消重排(同你现有实现)。

> 多 agent(你跑 Claude Code + Codex)进阶可选:把单一 `desired` 换成"每个 session_id 一个状态",对外显示 = 所有 session 中优先级最高者。这样"任一 agent 等你批权限"会立刻顶成 `permission`。第一版可不做。

---

## 3. 固件表情引擎规格(本次重点)

目标:把 `clawpet-face.html` 里的程序化机器眼 1:1 搬到固件。表情**不是图片**,是用圆角矩形/弧线/线段**画出来**的,所以不存在贴图转换,而是把网页里的绘制逻辑用 C/C++ 重写。数学(缓动、眨眼换装状态机)完全照搬。

### 3.1 渲染路线(二选一)

- **路线 A — LVGL(推荐)**:沿用微雪给本板的 LVGL 例程(SH8601 QSPI + FT3168 触摸驱动现成)。眼睛用 `lv_canvas` 自绘,或 `lv_draw_rect`(圆角)+ `lv_draw_arc` + `lv_draw_line`;过渡和待机微动作用 `lv_anim` / `lv_timer`。以后做触摸 UI、配网页面都方便。
- **路线 B — 裸帧缓冲**:PSRAM 开 framebuffer,自写 `fill_round_rect/draw_arc/draw_line`,每帧 QSPI 刷屏。最轻,和 canvas 代码几乎逐行对应。

两条路线的**状态机与参数表完全一致**,差别只在最底层画图原语。

### 3.2 每状态视觉参数表

屏幕逻辑坐标建议 368×448(与真机一致)。两眼水平居中,中心间距 `gap≈2×眼宽`。下表的 `eyeH` 是相对基准高度的倍数,`gazeY` 为像素偏移(正=向下看),`lid` 为从上方盖下的眼睑比例。颜色为眼睛/眉毛主色(在黑底上发光)。

| state | color | eyeShape | brow | pupil | mouth | eyeH | gazeY | lid | extra | motion | glow |
|---|---|---|---|---|---|---|---|---|---|---|---|
| idle | `#7fe9ff` | open | flat | none | smile | 1.0 | 0 | 0 | – | 眨眼/扫视/呼吸 | 0.18 |
| listening | `#aef3ff` | open | up | none | – | 1.1 | -2 | 0 | wave | – | 0.22 |
| thinking | `#7fe9ff` | open | up | none | – | 0.95 | -15 | 0 | dots | – | 0.18 |
| working | `#38e1ff` | open | focus | none | – | 0.6 | 16 | 0.32 | busy | – | 0.20 |
| searching | `#46c8ff` | open | flat | none | – | 0.7 | 2 | 0.1 | – | 水平快速扫视(dart) | 0.18 |
| browsing | `#34d3c0` | open | up | orbit | – | 1.0 | 0 | 0 | globe | 瞳孔绕圈 | 0.20 |
| danger | `#ff5a5a` | open | angry | shock | grit | 1.35 | -2 | 0 | warn | 抖动(shake) | 0.34 |
| permission | `#ffb020` | open | up | cute | o | 1.2 | -4 | 0 | excl | 上下轻浮(bob) | 0.34 |
| denied | `#6b7180` | open | worried | none | frown | 0.55 | 16 | 0.4 | – | – | 0.08 |
| bored | `#aeb4c6` | open | flat | none | flat | 0.5 | 6 | 0.45 | – | 缓慢左右游移(wander) | 0.10 |
| authok | `#2ee6a0` | wink | up | none | smile | 1.0 | 0 | 0 | sparkle | – | 0.24 |
| done | `#2ee6a0` | happy | up | none | smile | 1.0 | 0 | 0 | sparkle | – | 0.30 |
| subdone | `#59e0a0` | wink | flat | none | smile | 1.0 | 0 | 0 | – | – | 0.22 |
| error | `#ff6b6b` | squint | angry | none | frown | 1.0 | 0 | 0 | – | 抖动(shake) | 0.26 |
| rate | `#ff9d3f` | tired | tired | none | flat | 1.0 | 4 | 0 | sweat | – | 0.16 |
| compacting | `#8b7fff` | closed | none | none | – | 1.0 | 0 | 0 | pack | – | 0.14 |
| touched | `#ff7ab8` | happy | up | none | smile | 1.0 | 0 | 0 | heart | – | 0.26 |
| dizzy | `#b98bff` | spiral | wobble | none | wave | 1.0 | 0 | 0 | – | 抖动(shake) | 0.22 |
| sleep | `#7f87a8` | closed | none | none | – | 1.0 | 0 | 0 | zzz | 屏幕调暗(dim 0.5) | 0.08 |
| speaking | `#7fe9ff` | open | flat | none | talk | 1.0 | 0 | 0 | – | 嘴部开合 | 0.20 |

字段取值集合:
- **eyeShape**: `open`(圆角矩形)/ `happy`(上扬弧 ^)/ `closed`(下弯弧 ‿)/ `squint`(>< 内挤)/ `tired`(横向疲惫弧)/ `spiral`(螺旋)/ `wink`(左 happy + 右 open)。
- **brow**(眉毛,辨识度的关键): `none/up(挑高)/flat/focus(压低专注)/angry(内低外高)/worried(内高外低)/tired(低平细)/wobble(随时间上下摆)`。
- **pupil**: `none / shock(中央小黑点=缩瞳惊恐) / cute(白色高光点=卖萌) / orbit(暗瞳沿眼内绕圈)`。
- **mouth**: `none/smile/frown/flat/o(小圆口)/grit(咬牙:圆角矩形+竖齿线)/wave(波浪嘴)/talk(开合)`。
- **extra**: `wave(底部声波条) / dots(顶部…) / busy(底部三点轮闪) / globe(小地球环+绕点) / warn(⚠+一滴汗) / sweat(下落汗滴) / excl(头顶感叹号) / sparkle(星点) / pack(一圈打包转点) / heart(脸红+升起爱心) / zzz(漂浮 Z)`。

### 3.3 引擎核心逻辑(务必照此实现)

**眨眼换装过渡(自然过渡的关键)**:状态切换时不要直接换形状,而是"先闭眼→闭合瞬间换表情→再睁开"。所有形状突变都藏在闭眼那一下。
- 维护 `shown`(正在画的状态)、`queued`(目标状态)、`tphase∈[0,1]`(1=睁 0=闭)、`tdir∈{-1,0,1}`。
- 收到新状态:`queued=新; 若 shown≠queued 且 tdir≥0 则 tdir=-1`(开始闭眼)。
- 每帧:`tphase += tdir*dt/0.13`;到 `tphase≤0` 时令 `tphase=0; shown=queued; tdir=1`(完成换装,开始睁眼);到 `tphase≥1` 时 `tphase=1; tdir=0`。
- 眼睛高度乘以 `smoothstep(tphase)`;眉毛透明度乘以 `smoothstep(tphase)`。

**连续量缓动**:颜色(RGB 分量)、glow、gazeY 等用帧率无关插值 `a += (target-a)*(1-0.001^dt)`,这样即便不经眨眼也平滑。

**待机微动作(让它"活着")**:仅在 `open/wink` 且不在过渡中时触发——
- 随机眨眼:每 1.6–5s 一次,快速把眼高压到 ~8% 再弹回。
- 扫视(saccade):随机小幅水平偏移;`searching` 用更快更大的抖动(dart),`bored` 用更慢更大的游移(wander)。
- 呼吸:整体高度乘 `1+0.012·sin(t·1.6)`。

### 3.4 参考 C 骨架(与 `clawpet-face.html` 逐段对应)

```c
typedef enum { IDLE, LISTENING, THINKING, WORKING, SEARCHING, BROWSING, DANGER,
               PERMISSION, DENIED, BORED, AUTHOK, DONE, SUBDONE, ERROR, RATE,
               COMPACTING, TOUCHED, DIZZY, SLEEP, SPEAKING } mood_t;

typedef struct {
    uint16_t color; uint8_t shape, brow, pupil, mouth, extra;
    float eyeH, gazeY, lid, glow; uint8_t motion; bool dim;
} mood_def_t;
static const mood_def_t MOODS[/*20*/] = { /* 照 §3.2 表逐行填 */ };

static mood_t shown = IDLE, queued = IDLE;
static float  tphase = 1.0f; static int tdir = 0;
// + 连续量当前值: cur_color/cur_glow/cur_gazeY ...

void set_mood(mood_t m){ queued = m; if (shown!=m && tdir>=0) tdir=-1; }  // BLE 收到状态串后调用

static float smoothstep(float x){ if(x<0)x=0; if(x>1)x=1; return x*x*(3-2*x); }

void engine_tick(float dt){
    if (tdir){ tphase += tdir*dt/0.13f;
        if (tphase<=0 && tdir<0){ tphase=0; shown=queued; tdir=1; }
        if (tphase>=1 && tdir>0){ tphase=1; tdir=0; } }
    const mood_def_t *M = &MOODS[shown];
    float k = 1.0f - powf(0.001f, dt);
    // cur_color = lerp(cur_color, M->color, k);  cur_glow/gazeY 同理
    // 待机眨眼/扫视/呼吸: 仅 open|wink 且 tdir==0 时更新
}

void engine_render(void){          // 每帧调用
    float ease = smoothstep(tphase);
    int eyeH = (int)(BASE_H * MOODS[shown].eyeH * blink_scale * breathe * fmaxf(0.06f, ease));
    // 左右眼: draw_eye(...) 按 shape 分支(open=圆角矩形, happy/closed/tired=弧, squint=折线, spiral=螺旋)
    // draw_brows(..., alpha = ease)   ← 眉毛是辨识度关键, 别省
    // draw_mouth(...)  draw_extra(...)  ← 见 §3.2 extra 集合
}
```
> 把 `clawpet-face.html` 里 `MOODS / frame() / drawEye() / drawBrows() / draw()` 当作权威实现参考,逐函数翻译即可——浏览器里 `ctx.fill()` 对应固件的 `fill_round_rect` 或 `lv_draw_rect`,其余逻辑原样照搬。

---

## 4. 板载传感器态(touched / dizzy)

这两个**不经 hook、不经守护进程**,由固件直接产生,且短暂凌驾于当前表情之上(显示 ~2–4s 后回落到收到的最新 BLE 状态):
- 电容触摸(FT3168)检测到点按/抚摸 → `touched`。
- 六轴(QMI8658)检测到摇晃(加速度方差超阈值)→ `dizzy`。

实现:固件维护 `ble_state`(来自守护进程)与 `local_override`(传感器);渲染取二者中优先级高者(见 §2.2 PRIORITY),`local_override` 到期后清空,回到 `ble_state`。

---

## 5. 验收标准(Definition of Done)

1. 把 §2.1 hooks 合并进 `~/.claude/settings.json`(保留 notchi/vibeusage 行),`/hooks` 菜单能看到全部已挂载。
2. `deskbuddy` 的 `STATES/EVENT_MAP/瞬态优先级` 按 §2.2 升级,`deskbuddy set-state danger` 等能逐个点亮。
3. 固件实现 §3 的机器眼引擎:全部状态可渲染,**每个状态靠眉毛/眼型一眼可辨**,切换为"眨眼换装"过渡,待机有眨眼/扫视/呼吸。
4. 危险粘性:连续跑 `rm -rf` 测试命令时,`danger` 脸顶住 ≥3s 不被随后的 `working` 冲掉。
5. 瞬态回落:`done/error/...` 显示约 4s 后自动回到 `idle`。
6. 传感器:摸屏出 `touched`、摇晃出 `dizzy`,到期回落到 BLE 当前态。
7. 未实现状态按 §1 别名优雅降级,`danger` 必须实现、不降级。

## 6. 端到端测试清单

```bash
# 不依赖真机的逻辑测试:直接驱动守护进程
deskbuddy set-state permission     # 应卖萌+头顶感叹号
deskbuddy set-state danger         # 瞪眼+缩瞳+⚠+抖
deskbuddy event tool-failure       # → error, 约4s后回 idle
deskbuddy event stop-failure       # → rate(累了/限流), 不是 error
deskbuddy event subagent-stop      # → subdone(眨眼笑)

# 真机联调:在 Claude Code 里
#  - 提交一个 prompt           → thinking
#  - 让它编辑文件              → working;读代码 → searching;联网 → browsing
#  - 触发一次需要批准的操作    → permission(粘住直到你处理)
#  - 让它跑一条 rm -rf 测试目录 → danger 顶住数秒
#  - 任务完成                  → done → 4s 后 idle
#  - 结束会话                  → sleep
```

---

### 附:本次改动清单(给实现者打勾用)
- [ ] `settings.json`:PreToolUse 拆 matcher、加 Bash `if` 危险规则、加 PermissionDenied / Notification(idle_prompt,auth_success)/ PreCompact / SessionEnd 的 deskbuddy 行(notchi 行不动)
- [ ] `deskbuddy`:`STATES` 扩充、`EVENT_MAP` 覆盖、瞬态集合 + 粘性优先级 + 别名降级
- [ ] 固件:机器眼引擎(眉毛+眼型+瞳孔+嘴+extra)、眨眼换装过渡、待机微动作、传感器本地态
- [ ] 视觉对照 `clawpet-face.html` 调参直到神似
