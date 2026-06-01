/* moods.js — the 20 canonical face states, keyed by firmware face.cpp kNames[].
 *
 * Reconciled from docs/v2/clawpet-face.html MOODS (which used old keys awake/
 * waiting and lacked idle/bored/speaking). Colors/labels/visual params mirror
 * firmware/src/face.cpp MOODS[]/kLabel[] so this preview matches the device —
 * including the gaze-pupil eyeballs (pupil:'gaze') and the talk mouth.
 *
 * fields: label(中文) color shape brow pupil mouth eyeH eyeW gazeY lid extra
 *         glow [dim] [dart|wander|bob|shake]
 * shape: open|happy|closed|spiral|wink|squint|tired
 * brow:  none|up|flat|focus|angry|worried|tired|wobble
 * pupil: none|gaze|shock|cute|orbit
 * mouth: none|smile|frown|flat|grit|o|wave|talk
 */
window.MOODS = {
  idle:      {label:'待命',     color:'#7fe9ff', shape:'open',  brow:'flat',   pupil:'gaze',  mouth:'smile', eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'none',   glow:.18},
  listening: {label:'在听',     color:'#aef3ff', shape:'open',  brow:'up',     pupil:'none',  mouth:'none',  eyeH:1.1, eyeW:1.05, gazeY:-2, lid:0,   extra:'wave',   glow:.22},
  thinking:  {label:'思考中',   color:'#7fe9ff', shape:'open',  brow:'up',     pupil:'gaze',  mouth:'none',  eyeH:.95, eyeW:1,    gazeY:-15,lid:0,   extra:'dots',   glow:.18},
  working:   {label:'执行中',   color:'#38e1ff', shape:'open',  brow:'focus',  pupil:'gaze',  mouth:'none',  eyeH:.6,  eyeW:1,    gazeY:16, lid:.32, extra:'busy',   glow:.20},
  searching: {label:'读取中',   color:'#46c8ff', shape:'open',  brow:'flat',   pupil:'gaze',  mouth:'none',  eyeH:.7,  eyeW:1,    gazeY:2,  lid:.1,  extra:'none',   glow:.18, dart:1},
  browsing:  {label:'联网中',   color:'#34d3c0', shape:'open',  brow:'up',     pupil:'orbit', mouth:'none',  eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'globe',  glow:.20},
  danger:    {label:'危险！',   color:'#ff5a5a', shape:'open',  brow:'angry',  pupil:'shock', mouth:'grit',  eyeH:1.35,eyeW:1.15, gazeY:-2, lid:0,   extra:'warn',   glow:.34, shake:1},
  permission:{label:'等你确认', color:'#ffb020', shape:'open',  brow:'up',     pupil:'cute',  mouth:'o',     eyeH:1.2, eyeW:1.08, gazeY:-4, lid:0,   extra:'excl',   glow:.34, bob:1},
  denied:    {label:'被拒绝',   color:'#6b7180', shape:'open',  brow:'worried',pupil:'gaze',  mouth:'frown', eyeH:.55, eyeW:1,    gazeY:16, lid:.4,  extra:'none',   glow:.08},
  bored:     {label:'发呆中',   color:'#aeb4c6', shape:'open',  brow:'flat',   pupil:'gaze',  mouth:'flat',  eyeH:.5,  eyeW:1,    gazeY:6,  lid:.45, extra:'none',   glow:.10, wander:1},
  authok:    {label:'登录成功', color:'#2ee6a0', shape:'wink',  brow:'up',     pupil:'none',  mouth:'smile', eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'sparkle',glow:.24},
  done:      {label:'完成',     color:'#2ee6a0', shape:'happy', brow:'up',     pupil:'none',  mouth:'smile', eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'sparkle',glow:.30},
  subdone:   {label:'子任务完成',color:'#59e0a0',shape:'wink',  brow:'flat',   pupil:'none',  mouth:'smile', eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'none',   glow:.22},
  error:     {label:'出错了',   color:'#ff6b6b', shape:'squint',brow:'angry',  pupil:'none',  mouth:'frown', eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'none',   glow:.26, shake:1},
  rate:      {label:'被限流',   color:'#ff9d3f', shape:'tired', brow:'tired',  pupil:'none',  mouth:'flat',  eyeH:1,   eyeW:1,    gazeY:4,  lid:0,   extra:'sweat',  glow:.16},
  compacting:{label:'整理记忆', color:'#8b7fff', shape:'closed',brow:'none',   pupil:'none',  mouth:'none',  eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'pack',   glow:.14},
  touched:   {label:'摸摸头',   color:'#ff7ab8', shape:'happy', brow:'up',     pupil:'none',  mouth:'smile', eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'heart',  glow:.26},
  dizzy:     {label:'头晕了',   color:'#b98bff', shape:'spiral',brow:'wobble', pupil:'none',  mouth:'wave',  eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'none',   glow:.22, shake:1},
  sleep:     {label:'睡觉中',   color:'#7f87a8', shape:'open',  brow:'none',   pupil:'gaze',  mouth:'none',  eyeH:.42, eyeW:1,    gazeY:16, lid:.5,  extra:'zzz',    glow:.08, dim:.5},
  speaking:  {label:'说话中',   color:'#7fe9ff', shape:'open',  brow:'flat',   pupil:'none',  mouth:'talk',  eyeH:1,   eyeW:1,    gazeY:0,  lid:0,   extra:'none',   glow:.20},
};

/* Grid grouping for the console (canonical ids). */
window.GROUPS = [
  {h:'生命周期 · 输入',        keys:['idle','listening','sleep','speaking']},
  {h:'思考 & 干活',            keys:['thinking','working','searching','browsing','danger']},
  {h:'要你出手',               keys:['permission','denied','bored','authok']},
  {h:'结果 / 异常 / 上下文',   keys:['done','subdone','error','rate','compacting']},
  {h:'桌宠人格 · 传感器',      keys:['touched','dizzy']},
];

/* States whose arrival makes the device chirp a cue tone. */
window.CUE_STATES = ['permission','done','error','danger'];
