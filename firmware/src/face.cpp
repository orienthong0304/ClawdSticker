#include "face.h"
#include "hal/board_caps.h"
#include <Arduino.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_zh_22);                     // Chinese status-line subset

// ─────────────────────────────────────────────────────────────────────────────
// All-LVGL "machine eye" engine (clawpet v2). Ported from docs/v2/clawpet-face.html.
// The HTML draws in a 284×346 logical canvas; we render to the native 368×448
// panel with a uniform scale S = W/284 (aspect ratios match to <0.1%), so every
// HTML constant stays literal and just gets *S via Z().
//
// Rendering is LVGL-native: each face part is a persistent lv_obj / lv_line /
// lv_arc / lv_image created once; a 30 fps lv_timer updates only their geometry,
// colour and opacity. LVGL repaints just the dirty rectangles with anti-aliasing
// — no full-screen software framebuffer (that was QSPI/PSRAM-bound at ~8 fps).
// Curves (happy/closed/tired eyes, smile/frown/wave mouths) are lv_line polylines
// sampled from the same quadratic béziers the HTML uses.
// ─────────────────────────────────────────────────────────────────────────────

// Mood field enums (match the HTML string tags).
enum { SH_OPEN, SH_HAPPY, SH_CLOSED, SH_SQUINT, SH_TIRED, SH_SPIRAL, SH_WINK };
enum { BR_NONE, BR_UP, BR_FLAT, BR_FOCUS, BR_ANGRY, BR_WORRIED, BR_TIRED, BR_WOBBLE };
enum { PU_NONE, PU_SHOCK, PU_CUTE, PU_ORBIT, PU_GAZE };
enum { MO_NONE, MO_SMILE, MO_FROWN, MO_FLAT, MO_O, MO_GRIT, MO_WAVE, MO_TALK };
enum { EX_NONE, EX_WAVE, EX_DOTS, EX_BUSY, EX_GLOBE, EX_WARN, EX_SWEAT,
       EX_EXCL, EX_SPARKLE, EX_PACK, EX_HEART, EX_ZZZ };
enum { M_BOB = 1, M_SHAKE = 2, M_DART = 4, M_WANDER = 8 };  // motion flags

struct MoodDef {
    uint32_t color;                 // 0xRRGGBB
    uint8_t  shape, brow, pupil, mouth, extra, motion;
    float    eyeH, eyeW, gazeY, lid, glow, dim;
};

// Indexed by face_state_t. Values lifted from clawpet-face.html MOODS + §3.2.
static const MoodDef MOODS[FACE_STATE_COUNT] = {
//   color      shape      brow       pupil     mouth     extra       motion   eyeH eyeW gazeY  lid  glow  dim
/*IDLE*/      {0x7fe9ff, SH_OPEN,   BR_FLAT,   PU_GAZE,  MO_SMILE, EX_NONE,    0,        1.00,1.00,  0,  0.00,0.18,0},
/*LISTENING*/ {0xaef3ff, SH_OPEN,   BR_UP,     PU_NONE,  MO_NONE,  EX_WAVE,    0,        1.10,1.05, -2,  0.00,0.22,0},
/*THINKING*/  {0x7fe9ff, SH_OPEN,   BR_UP,     PU_GAZE,  MO_NONE,  EX_DOTS,    0,        0.95,1.00,-15,  0.00,0.18,0},
/*WORKING*/   {0x38e1ff, SH_OPEN,   BR_FOCUS,  PU_GAZE,  MO_NONE,  EX_BUSY,    0,        0.60,1.00, 16,  0.32,0.20,0},
/*SEARCHING*/ {0x46c8ff, SH_OPEN,   BR_FLAT,   PU_GAZE,  MO_NONE,  EX_NONE,    M_DART,   0.70,1.00,  2,  0.10,0.18,0},
/*BROWSING*/  {0x34d3c0, SH_OPEN,   BR_UP,     PU_ORBIT, MO_NONE,  EX_GLOBE,   0,        1.00,1.00,  0,  0.00,0.20,0},
/*DANGER*/    {0xff5a5a, SH_OPEN,   BR_ANGRY,  PU_SHOCK, MO_GRIT,  EX_WARN,    M_SHAKE,  1.35,1.15, -2,  0.00,0.34,0},
/*PERMISSION*/{0xffb020, SH_OPEN,   BR_UP,     PU_CUTE,  MO_O,     EX_EXCL,    M_BOB,    1.20,1.08, -4,  0.00,0.34,0},
/*DENIED*/    {0x6b7180, SH_OPEN,   BR_WORRIED,PU_GAZE,  MO_FROWN, EX_NONE,    0,        0.55,1.00, 16,  0.40,0.08,0},
/*BORED*/     {0xaeb4c6, SH_OPEN,   BR_FLAT,   PU_GAZE,  MO_FLAT,  EX_NONE,    M_WANDER, 0.50,1.00,  6,  0.45,0.10,0},
/*AUTHOK*/    {0x2ee6a0, SH_WINK,   BR_UP,     PU_NONE,  MO_SMILE, EX_SPARKLE, 0,        1.00,1.00,  0,  0.00,0.24,0},
/*DONE*/      {0x2ee6a0, SH_HAPPY,  BR_UP,     PU_NONE,  MO_SMILE, EX_SPARKLE, 0,        1.00,1.00,  0,  0.00,0.30,0},
/*SUBDONE*/   {0x59e0a0, SH_WINK,   BR_FLAT,   PU_NONE,  MO_SMILE, EX_NONE,    0,        1.00,1.00,  0,  0.00,0.22,0},
/*ERROR*/     {0xff6b6b, SH_SQUINT, BR_ANGRY,  PU_NONE,  MO_FROWN, EX_NONE,    M_SHAKE,  1.00,1.00,  0,  0.00,0.26,0},
/*RATE*/      {0xff9d3f, SH_TIRED,  BR_TIRED,  PU_NONE,  MO_FLAT,  EX_SWEAT,   0,        1.00,1.00,  4,  0.00,0.16,0},
/*COMPACTING*/{0x8b7fff, SH_CLOSED, BR_NONE,   PU_NONE,  MO_NONE,  EX_PACK,    0,        1.00,1.00,  0,  0.00,0.14,0},
/*TOUCHED*/   {0xff7ab8, SH_HAPPY,  BR_UP,     PU_NONE,  MO_SMILE, EX_HEART,   0,        1.00,1.00,  0,  0.00,0.26,0},
/*DIZZY*/     {0xb98bff, SH_SPIRAL, BR_WOBBLE, PU_NONE,  MO_WAVE,  EX_NONE,    M_SHAKE,  1.00,1.00,  0,  0.00,0.22,0},
/*SLEEP*/     {0x7f87a8, SH_OPEN,   BR_NONE,   PU_GAZE,  MO_NONE,  EX_ZZZ,     0,        0.42,1.00, 16,  0.50,0.08,0.5},
/*SPEAKING*/  {0x7fe9ff, SH_OPEN,   BR_FLAT,   PU_NONE,  MO_TALK,  EX_NONE,    0,        1.00,1.00,  0,  0.00,0.20,0},
};

// Arbitration priority (host vs board-local sensor mood), per docs/v2 §2.2.
static const uint8_t PRIO[FACE_STATE_COUNT] = {
/*IDLE*/5,/*LIST*/12,/*THINK*/15,/*WORK*/20,/*SEARCH*/20,/*BROWSE*/20,
/*DANGER*/100,/*PERM*/90,/*DENIED*/70,/*BORED*/12,/*AUTHOK*/50,/*DONE*/50,
/*SUBDONE*/50,/*ERROR*/60,/*RATE*/60,/*COMPACT*/30,/*TOUCHED*/80,/*DIZZY*/80,
/*SLEEP*/10,/*SPEAK*/18,
};

static const char* kNames[FACE_STATE_COUNT] = {
    "idle","listening","thinking","working","searching","browsing","danger",
    "permission","denied","bored","authok","done","subdone","error","rate",
    "compacting","touched","dizzy","sleep","speaking",
};

// Chinese status line under the face (says what Claude Code is doing — the face
// alone is ambiguous). Every glyph here must be in font_zh_22's subset.
static const char* kLabel[FACE_STATE_COUNT] = {
    "待命", "在听", "思考中", "执行中", "读取中", "联网中",
    "危险！", "等你确认", "被拒绝", "发呆中", "登录成功", "完成",
    "子任务完成", "出错了", "被限流", "整理记忆", "摸摸头", "头晕了",
    "睡觉中", "说话中",
};

// ─── Geometry ────────────────────────────────────────────────────────────────
static int   W, H, CX, CY;
static float S;
#define Z(v)  ((float)(v) * S)

// ─── Engine state (mirrors the HTML `A` accumulator + transition vars) ───────
static face_state_t host_state = FACE_IDLE;
static face_state_t shown = FACE_IDLE, queued = FACE_IDLE;
static face_state_t ovr_state = FACE_IDLE;
static uint32_t     ovr_until = 0;
static float tphase = 1.0f;       // 1=eyes open, 0=closed (blink-changeover)
static int   tdir   = 0;          // -1 closing, +1 opening, 0 steady
static struct { float eyeH, eyeW, gazeX, gazeY, lid, glow, dim, r, g, b; }
    A = {1,1,0,0,0,0.18f,0, 0x7f,0xe9,0xff};
static float blink = 0, blinkT = 1.2f, sacT = 2.0f, sacX = 0;
static float tsec = 0;
static uint32_t last_ms = 0;

// ─── LVGL objects (created once, restyled per frame) ─────────────────────────
static lv_obj_t* root  = nullptr;
static lv_timer_t* tmr = nullptr;
static lv_obj_t* status_lbl = nullptr;          // status text under the face

static lv_obj_t* glow_img = nullptr;            // pre-dithered RGB565 radial halo
static lv_image_dsc_t glow_dsc;
static uint16_t* glow_rgb = nullptr;
static int       GLOW_D = 0;

// eyes: idx 0=left 1=right
static lv_obj_t* e_body[2];                     // open: rounded rect
static lv_obj_t* e_lid[2];                      // working: top lid (child)
static lv_obj_t* e_pupil[2];                    // shock/orbit dark, cute white (child)
static lv_obj_t* e_pupil2[2];                   // cute: second white dot (child)
static lv_obj_t* e_curve[2];                    // happy/closed/tired: bézier line
static lv_obj_t* e_sq[2][2];                    // squint: 2 lines
static lv_obj_t* e_spiral[2];                   // spiral: polyline
static lv_obj_t* brow[2];                       // brows
static lv_obj_t* m_curve = nullptr;             // smile/frown/flat/wave
static lv_obj_t* m_o = nullptr;                 // o (ring)
static lv_obj_t* m_rect = nullptr;              // talk
static lv_obj_t* m_grit = nullptr;              // grit box
static lv_obj_t* m_teeth[2];                    // grit teeth
static lv_obj_t* ex_dot[3];                     // dots / busy
static lv_obj_t* ex_spark[3];                   // sparkle
static lv_obj_t* ex_pack[8];                    // pack ring
static lv_obj_t* ex_globe = nullptr;            // globe ring
static lv_obj_t* ex_globe_dot = nullptr;
static lv_obj_t* ex_warn = nullptr;             // warning triangle (A8 image)
static lv_image_dsc_t warn_dsc; static uint8_t* warn_a8 = nullptr;
static lv_obj_t* ex_sweat = nullptr;
static lv_obj_t* ex_excl_bar = nullptr, *ex_excl_dot = nullptr;
static lv_obj_t* ex_blush[2];
static lv_obj_t* ex_heart[3];
static lv_obj_t* ex_zzz[3];
static lv_obj_t* ex_wave[3];

// Persistent point arrays (lv_line keeps the pointer, not a copy).
#define QN 11
#define SPN 16
static lv_point_precise_t pE_curve[2][QN];
static lv_point_precise_t pE_sq[2][2][2];
static lv_point_precise_t pE_spiral[2][SPN];
static lv_point_precise_t pBrow[2][2];
static lv_point_precise_t pMouth[QN];

static int cur_shape = -1, cur_brow, cur_pupil, cur_mouth, cur_extra;
static face_state_t cfg_for = (face_state_t)-1;

// ─── Small helpers ───────────────────────────────────────────────────────────
static inline lv_color_t col_now(void) { return lv_color_make((int)A.r, (int)A.g, (int)A.b); }
static inline float ssmooth(float x) { if (x < 0) x = 0; if (x > 1) x = 1; return x * x * (3 - 2 * x); }
static inline float frand(void) { return (float)random(0, 1001) / 1000.0f; }
static inline float flerp(float a, float b, float k) { return a + (b - a) * k; }
static inline void show(lv_obj_t* o, bool v) {
    if (!o) return;
    if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static inline void place_box(lv_obj_t* o, float cx, float cy, float w, float h) {
    lv_obj_set_size(o, (int)(w + 0.5f), (int)(h + 0.5f));
    lv_obj_set_pos(o, (int)(cx - w / 2 + 0.5f), (int)(cy - h / 2 + 0.5f));
}
static void quad_pts(lv_point_precise_t* p, int n,
                     float x0, float y0, float cx, float cy, float x1, float y1) {
    for (int i = 0; i < n; i++) {
        float t = (float)i / (n - 1), u = 1 - t;
        p[i].x = (int32_t)(u * u * x0 + 2 * u * t * cx + t * t * x1 + 0.5f);
        p[i].y = (int32_t)(u * u * y0 + 2 * u * t * cy + t * t * y1 + 0.5f);
    }
}

// ─── Object factories ────────────────────────────────────────────────────────
static lv_obj_t* mk_rect(lv_obj_t* par) {
    lv_obj_t* o = lv_obj_create(par);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}
static lv_obj_t* mk_circle(lv_obj_t* par) {
    lv_obj_t* o = mk_rect(par);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
}
static lv_obj_t* mk_line(lv_obj_t* par, float w) {
    lv_obj_t* o = lv_line_create(par);
    lv_obj_set_pos(o, 0, 0);                       // points carry absolute coords
    lv_obj_set_style_line_width(o, (int)(w + 0.5f), 0);
    lv_obj_set_style_line_rounded(o, true, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static inline uint16_t pack565(int r, int g, int b) {
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ─── Glow halo ────────────────────────────────────────────────────────────────
// A radial gradient over pure black bands badly in RGB565 (only 8/4/8 levels per
// channel), and LVGL doesn't dither. So we bake the halo into a pre-dithered
// RGB565 image (Bayer 4×4) and only regenerate it when the mood changes — the
// glow is a dim ambient halo, so snapping its colour at the changeover is
// imperceptible, and there's zero per-frame cost.
static const uint8_t BAYER4[16] = {0,8,2,10, 12,4,14,6, 3,11,1,9, 15,7,13,5};
static void regen_glow(uint32_t color, float intensity) {
    int D = GLOW_D, R = (color >> 16) & 0xFF, G = (color >> 8) & 0xFF, B = color & 0xFF;
    float c = D / 2.0f, rad = D / 2.0f, r2 = rad * rad;
    if (intensity < 0) intensity = 0;
    for (int y = 0; y < D; y++)
        for (int x = 0; x < D; x++) {
            float dx = x - c, dy = y - c, d2 = dx * dx + dy * dy;
            uint16_t px = 0;
            if (d2 < r2) {
                float f = powf(1.0f - d2 / r2, 1.6f), a = intensity * f;
                float dth = BAYER4[(y & 3) * 4 + (x & 3)] / 16.0f - 0.5f;
                px = pack565((int)(R * a + dth * 8),
                             (int)(G * a + dth * 4),
                             (int)(B * a + dth * 8));
            }
            glow_rgb[y * D + x] = px;
        }
    if (glow_img) lv_obj_invalidate(glow_img);
}
static void gen_glow_image(void) {
    GLOW_D = (int)(Z(150) * 2);
    glow_rgb = (uint16_t*)heap_caps_malloc((size_t)GLOW_D * GLOW_D * 2, MALLOC_CAP_SPIRAM);
    lv_memzero(&glow_dsc, sizeof(glow_dsc));
    glow_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    glow_dsc.header.cf     = LV_COLOR_FORMAT_RGB565;
    glow_dsc.header.w      = GLOW_D;
    glow_dsc.header.h      = GLOW_D;
    glow_dsc.header.stride = GLOW_D * 2;
    glow_dsc.data          = (const uint8_t*)glow_rgb;
    glow_dsc.data_size     = (uint32_t)GLOW_D * GLOW_D * 2;
    regen_glow(MOODS[FACE_IDLE].color, MOODS[FACE_IDLE].glow);
    glow_img = lv_image_create(root);
    lv_image_set_src(glow_img, &glow_dsc);
    lv_obj_set_pos(glow_img, CX - GLOW_D / 2, CY - GLOW_D / 2);
}

// Small filled "⚠" triangle as an A8 image (fixed amber; shown in danger).
static void gen_warn_image(void) {
    int D = (int)(Z(34));
    warn_a8 = (uint8_t*)heap_caps_malloc((size_t)D * D, MALLOC_CAP_SPIRAM);
    memset(warn_a8, 0, (size_t)D * D);
    for (int y = 0; y < D; y++) {
        float t = (float)y / (D - 1);
        int half = (int)(t * D / 2.0f);
        for (int x = D / 2 - half; x <= D / 2 + half; x++)
            if (x >= 0 && x < D) warn_a8[y * D + x] = 255;
    }
    // punch a dark "!" by zeroing a slim column + gap (drawn via recolor bg).
    lv_memzero(&warn_dsc, sizeof(warn_dsc));
    warn_dsc.header.magic  = LV_IMAGE_HEADER_MAGIC;
    warn_dsc.header.cf     = LV_COLOR_FORMAT_A8;
    warn_dsc.header.w      = D;
    warn_dsc.header.h      = D;
    warn_dsc.header.stride = D;
    warn_dsc.data          = warn_a8;
    warn_dsc.data_size     = (uint32_t)D * D;
    ex_warn = lv_image_create(root);
    lv_image_set_src(ex_warn, &warn_dsc);
    lv_obj_set_style_image_recolor(ex_warn, lv_color_hex(0xffd23f), 0);
    lv_obj_set_style_image_recolor_opa(ex_warn, LV_OPA_COVER, 0);
    lv_obj_add_flag(ex_warn, LV_OBJ_FLAG_HIDDEN);
}

// ─── Mood configuration (called when `shown` changes) ────────────────────────
static int eye_shape_for(int shape, int idx) {
    if (shape == SH_WINK) return idx == 0 ? SH_HAPPY : SH_OPEN;
    return shape;
}
static void hide_all_parts(void) {
    for (int i = 0; i < 2; i++) {
        show(e_body[i], false); show(e_curve[i], false); show(e_spiral[i], false);
        show(e_sq[i][0], false); show(e_sq[i][1], false);
        show(e_pupil[i], false); show(e_pupil2[i], false); show(e_lid[i], false);
        show(brow[i], false);
    }
    show(m_curve, false); show(m_o, false); show(m_rect, false);
    show(m_grit, false); show(m_teeth[0], false); show(m_teeth[1], false);
    for (int i = 0; i < 3; i++) { show(ex_dot[i], false); show(ex_spark[i], false);
        show(ex_heart[i], false); show(ex_zzz[i], false); show(ex_wave[i], false); }
    for (int i = 0; i < 8; i++) show(ex_pack[i], false);
    show(ex_globe, false); show(ex_globe_dot, false); show(ex_warn, false);
    show(ex_sweat, false); show(ex_excl_bar, false); show(ex_excl_dot, false);
    show(ex_blush[0], false); show(ex_blush[1], false);
}
static void configure_mood(face_state_t m) {
    const MoodDef* M = &MOODS[m];
    cur_shape = M->shape; cur_brow = M->brow; cur_pupil = M->pupil;
    cur_mouth = M->mouth; cur_extra = M->extra;
    regen_glow(M->color, M->glow * (1.0f - M->dim));   // snap halo to the new mood
    if (status_lbl) {
        lv_label_set_text(status_lbl, kLabel[m]);
        lv_obj_set_style_text_color(status_lbl,
            lv_color_make((M->color >> 16) & 0xFF, (M->color >> 8) & 0xFF, M->color & 0xFF), 0);
    }
    hide_all_parts();

    for (int i = 0; i < 2; i++) {
        int sh = eye_shape_for(cur_shape, i);
        switch (sh) {
        case SH_OPEN:
            show(e_body[i], true);
            show(e_pupil[i], cur_pupil != PU_NONE);
            show(e_pupil2[i], cur_pupil == PU_CUTE || cur_pupil == PU_GAZE);
            break;
        case SH_HAPPY: case SH_CLOSED: case SH_TIRED:
            lv_obj_set_style_line_width(e_curve[i], (int)(sh == SH_TIRED ? Z(9) : Z(11)), 0);
            show(e_curve[i], true); break;
        case SH_SQUINT:
            show(e_sq[i][0], true); show(e_sq[i][1], true); break;
        case SH_SPIRAL:
            show(e_spiral[i], true); break;
        }
    }
    if (cur_brow != BR_NONE) { show(brow[0], true); show(brow[1], true); }

    switch (cur_mouth) {
    case MO_SMILE: case MO_FROWN: case MO_FLAT: case MO_WAVE:
        lv_obj_set_style_line_width(m_curve, (int)Z(cur_mouth == MO_WAVE ? 5 : 7), 0);
        show(m_curve, true); break;
    case MO_O:    show(m_o, true); break;
    case MO_TALK: show(m_rect, true); break;
    case MO_GRIT: show(m_grit, true); show(m_teeth[0], true); show(m_teeth[1], true); break;
    }

    switch (cur_extra) {
    case EX_DOTS: case EX_BUSY: for (int i = 0; i < 3; i++) show(ex_dot[i], true); break;
    case EX_SPARKLE: for (int i = 0; i < 3; i++) show(ex_spark[i], true); break;
    case EX_PACK: for (int i = 0; i < 8; i++) show(ex_pack[i], true); break;
    case EX_GLOBE: show(ex_globe, true); show(ex_globe_dot, true); break;
    case EX_WARN: show(ex_warn, true); break;
    case EX_SWEAT: show(ex_sweat, true); break;
    case EX_EXCL: show(ex_excl_bar, true); show(ex_excl_dot, true); break;
    case EX_HEART: show(ex_blush[0], true); show(ex_blush[1], true);
                   for (int i = 0; i < 3; i++) show(ex_heart[i], true); break;
    case EX_ZZZ: for (int i = 0; i < 3; i++) show(ex_zzz[i], true); break;
    case EX_WAVE: for (int i = 0; i < 3; i++) show(ex_wave[i], true); break;
    }
}

// ─── Per-frame engine update (no LVGL calls; pure math) ──────────────────────
static void engine_tick(float dt) {
    if (dt > 0.05f) dt = 0.05f;
    tsec += dt;

    if (ovr_until && (int32_t)(millis() - ovr_until) >= 0) {
        ovr_until = 0;
        face_state_t pick = host_state;
        queued = pick; if (shown != pick && tdir >= 0) tdir = -1;
    }

    if (tdir != 0) {
        tphase += tdir * dt / 0.13f;
        if (tphase <= 0 && tdir < 0) { tphase = 0; shown = queued; tdir = 1; }
        if (tphase >= 1 && tdir > 0) { tphase = 1; tdir = 0; }
    }

    const MoodDef* M = &MOODS[shown];
    float k = 1.0f - powf(0.001f, dt);
    A.eyeH = flerp(A.eyeH, M->eyeH, k);
    A.eyeW = flerp(A.eyeW, M->eyeW, k);
    A.gazeY = flerp(A.gazeY, M->gazeY, k);
    A.lid  = flerp(A.lid, M->lid, k);
    A.glow = flerp(A.glow, M->glow, k);
    A.dim  = flerp(A.dim, M->dim, k);
    A.r = flerp(A.r, (M->color >> 16) & 0xFF, k);
    A.g = flerp(A.g, (M->color >> 8) & 0xFF, k);
    A.b = flerp(A.b, M->color & 0xFF, k);

    bool openish = (M->shape == SH_OPEN || M->shape == SH_WINK);
    blinkT -= dt;
    if (blinkT <= 0 && openish && tdir == 0) { blink = 1; blinkT = 1.6f + frand() * 3.4f; }
    if (blink > 0) { blink -= dt * 7; if (blink < 0) blink = 0; }

    sacT -= dt;
    if (sacT <= 0) {
        if (M->motion & M_DART)        { sacX = (frand() * 2 - 1) * 30; sacT = 0.2f + frand() * 0.2f; }
        else if (M->motion & M_WANDER) { sacX = (frand() * 2 - 1) * 26; sacT = 1.0f + frand() * 1.2f; }
        else                           { sacX = (frand() * 2 - 1) * 7;  sacT = 2.6f + frand() * 3; }
    }
    A.gazeX = flerp(A.gazeX, sacX, (M->motion & M_DART) ? k : k * 0.6f);
}

// ─── Per-frame render: push the eased state into the LVGL objects ────────────
static lv_opa_t OA(float a) { if (a < 0) a = 0; if (a > 1) a = 1; return (lv_opa_t)(a * 255); }

static void render_eye(int idx, int sh, float ex, float cy, float bw, float bh,
                       lv_color_t c, lv_opa_t base, float t) {
    if (sh == SH_OPEN) {
        place_box(e_body[idx], ex, cy, bw, bh);
        lv_obj_set_style_radius(e_body[idx], (int)(bw * 0.42f), 0);
        lv_obj_set_style_bg_color(e_body[idx], c, 0);
        lv_obj_set_style_opa(e_body[idx], base, 0);
        // top lid (working)
        if (A.lid > 0.02f) {
            int lh = (int)(bh * A.lid);
            lv_obj_set_size(e_lid[idx], (int)bw + 4, lh);
            lv_obj_set_pos(e_lid[idx], -2, -2);
            lv_obj_set_style_bg_color(e_lid[idx], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(e_lid[idx], LV_OPA_COVER, 0);
            show(e_lid[idx], true);
        } else show(e_lid[idx], false);
        // pupils (children, coords relative to e_body)
        if (cur_pupil == PU_SHOCK) {
            float r = bw * 0.17f; place_box(e_pupil[idx], bw / 2, bh / 2, 2 * r, 2 * r);
            lv_obj_set_style_bg_color(e_pupil[idx], lv_color_black(), 0);
            lv_obj_set_style_opa(e_pupil[idx], base, 0);
        } else if (cur_pupil == PU_ORBIT) {
            float a = t * 3, r = bw * 0.2f;
            place_box(e_pupil[idx], bw / 2 + cosf(a) * bw * 0.18f, bh / 2 + sinf(a) * bh * 0.16f, 2 * r, 2 * r);
            lv_obj_set_style_bg_color(e_pupil[idx], lv_color_black(), 0);
            lv_obj_set_style_opa(e_pupil[idx], base, 0);
        } else if (cur_pupil == PU_CUTE) {
            float r = bw * 0.13f;
            place_box(e_pupil[idx], bw * 0.36f, bh * 0.34f, 2 * r, 2 * r);
            lv_obj_set_style_bg_color(e_pupil[idx], lv_color_white(), 0);
            lv_obj_set_style_opa(e_pupil[idx], base, 0);
            float r2 = bw * 0.07f;
            place_box(e_pupil2[idx], bw * 0.66f, bh * 0.56f, 2 * r2, 2 * r2);
            lv_obj_set_style_bg_color(e_pupil2[idx], lv_color_white(), 0);
            lv_obj_set_style_opa(e_pupil2[idx], (lv_opa_t)(base * 0.7f), 0);
        } else if (cur_pupil == PU_GAZE) {
            // Dark eyeball that "looks": gaze maps into the eye-local travel box,
            // so the body holds still while the pupil tracks (see engine_render).
            float nx = A.gazeX / 30.0f; if (nx < -1) nx = -1; if (nx > 1) nx = 1;
            float ny = A.gazeY / 18.0f; if (ny < -1) ny = -1; if (ny > 1) ny = 1;
            float pr = bw * 0.18f;
            float mx = bw * 0.5f - pr - Z(3); if (mx < 0) mx = 0;
            float my = bh * 0.5f - pr - Z(3); if (my < 0) my = 0;
            float px = bw / 2 + nx * mx, py = bh / 2 + ny * my;
            place_box(e_pupil[idx], px, py, 2 * pr, 2 * pr);
            lv_obj_set_style_bg_color(e_pupil[idx], lv_color_hex(0x04050a), 0);
            lv_obj_set_style_opa(e_pupil[idx], base, 0);
            float cr = pr * 0.42f;                          // upper-left catchlight
            place_box(e_pupil2[idx], px - pr * 0.36f, py - pr * 0.42f, 2 * cr, 2 * cr);
            lv_obj_set_style_bg_color(e_pupil2[idx], lv_color_white(), 0);
            lv_obj_set_style_opa(e_pupil2[idx], base, 0);
        }
    } else if (sh == SH_HAPPY) {
        quad_pts(pE_curve[idx], QN, ex - Z(26), cy + Z(8), ex, cy - Z(26), ex + Z(26), cy + Z(8));
        lv_line_set_points(e_curve[idx], pE_curve[idx], QN);
        lv_obj_set_style_line_color(e_curve[idx], c, 0); lv_obj_set_style_opa(e_curve[idx], base, 0);
    } else if (sh == SH_CLOSED) {
        quad_pts(pE_curve[idx], QN, ex - Z(26), cy - Z(2), ex, cy + Z(16), ex + Z(26), cy - Z(2));
        lv_line_set_points(e_curve[idx], pE_curve[idx], QN);
        lv_obj_set_style_line_color(e_curve[idx], c, 0); lv_obj_set_style_opa(e_curve[idx], base, 0);
    } else if (sh == SH_TIRED) {
        quad_pts(pE_curve[idx], QN, ex - Z(24), cy, ex, cy + Z(5), ex + Z(24), cy);
        lv_line_set_points(e_curve[idx], pE_curve[idx], QN);
        lv_obj_set_style_line_color(e_curve[idx], c, 0); lv_obj_set_style_opa(e_curve[idx], base, 0);
    } else if (sh == SH_SQUINT) {
        float d = idx == 0 ? 1 : -1;
        pE_sq[idx][0][0].x = ex - Z(18) * d; pE_sq[idx][0][0].y = cy - Z(16);
        pE_sq[idx][0][1].x = ex + Z(14) * d; pE_sq[idx][0][1].y = cy;
        pE_sq[idx][1][0].x = ex + Z(14) * d; pE_sq[idx][1][0].y = cy;
        pE_sq[idx][1][1].x = ex - Z(18) * d; pE_sq[idx][1][1].y = cy + Z(16);
        for (int k = 0; k < 2; k++) {
            lv_line_set_points(e_sq[idx][k], pE_sq[idx][k], 2);
            lv_obj_set_style_line_color(e_sq[idx][k], c, 0);
            lv_obj_set_style_opa(e_sq[idx][k], base, 0);
        }
    } else if (sh == SH_SPIRAL) {
        float ang = t * 6;
        for (int i = 0; i < SPN; i++) {
            float th = (float)i / (SPN - 1) * (float)(M_PI * 3);
            float rr = Z(2) + Z(th * 2.6f / (M_PI));     // grow with angle
            pE_spiral[idx][i].x = ex + cosf(th + ang) * rr;
            pE_spiral[idx][i].y = cy + sinf(th + ang) * rr;
        }
        lv_line_set_points(e_spiral[idx], pE_spiral[idx], SPN);
        lv_obj_set_style_line_color(e_spiral[idx], c, 0);
        lv_obj_set_style_opa(e_spiral[idx], base, 0);
    }
}

static void render_brows(float lx, float rx, float topY, lv_color_t c, lv_opa_t a) {
    if (cur_brow == BR_NONE) return;
    float iy, oy;
    switch (cur_brow) {
    case BR_UP:      iy = topY - Z(9); oy = topY - Z(9); break;
    case BR_FOCUS:   iy = topY + Z(7); oy = topY + Z(2); break;
    case BR_ANGRY:   iy = topY + Z(10); oy = topY - Z(7); break;
    case BR_WORRIED: iy = topY - Z(10); oy = topY + Z(5); break;
    case BR_TIRED:   iy = topY + Z(6); oy = topY + Z(6); break;
    case BR_WOBBLE:  iy = topY + sinf(tsec * 8) * Z(4); oy = topY - sinf(tsec * 8) * Z(4); break;
    default:         iy = topY; oy = topY; break;   // BR_FLAT
    }
    pBrow[0][0].x = lx - Z(24); pBrow[0][0].y = oy; pBrow[0][1].x = lx + Z(16); pBrow[0][1].y = iy;
    pBrow[1][0].x = rx + Z(24); pBrow[1][0].y = oy; pBrow[1][1].x = rx - Z(16); pBrow[1][1].y = iy;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_line_width(brow[i], (int)Z(cur_brow == BR_TIRED ? 5 : 7), 0);
        lv_line_set_points(brow[i], pBrow[i], 2);
        lv_obj_set_style_line_color(brow[i], c, 0);
        lv_obj_set_style_opa(brow[i], a, 0);
    }
}

static void render_mouth(float my, lv_color_t c, lv_opa_t a, float t) {
    switch (cur_mouth) {
    case MO_SMILE: quad_pts(pMouth, QN, CX - Z(22), my - Z(6), CX, my + Z(13), CX + Z(22), my - Z(6));
                   lv_line_set_points(m_curve, pMouth, QN); break;
    case MO_FROWN: quad_pts(pMouth, QN, CX - Z(20), my + Z(8), CX, my - Z(8), CX + Z(20), my + Z(8));
                   lv_line_set_points(m_curve, pMouth, QN); break;
    case MO_FLAT:  pMouth[0].x = CX - Z(15); pMouth[0].y = my; pMouth[1].x = CX + Z(15); pMouth[1].y = my;
                   lv_line_set_points(m_curve, pMouth, 2); break;
    case MO_WAVE: { // two half-quads stitched: down then up
                   lv_point_precise_t* p = pMouth;
                   for (int i = 0; i < 5; i++) { float tt = i / 4.0f, u = 1 - tt;
                       p[i].x = u*u*(CX-Z(18)) + 2*u*tt*(CX-Z(9)) + tt*tt*CX;
                       p[i].y = u*u*my + 2*u*tt*(my-Z(7)) + tt*tt*my; }
                   for (int i = 0; i < 5; i++) { float tt = i / 4.0f, u = 1 - tt;
                       p[5+i].x = u*u*CX + 2*u*tt*(CX+Z(9)) + tt*tt*(CX+Z(18));
                       p[5+i].y = u*u*my + 2*u*tt*(my+Z(7)) + tt*tt*my; }
                   lv_line_set_points(m_curve, pMouth, 10); break; }
    case MO_O:     { float r = Z(7); place_box(m_o, CX, my, 2*r, 2*r);
                   lv_obj_set_style_border_color(m_o, c, 0);
                   lv_obj_set_style_opa(m_o, a, 0); return; }
    case MO_TALK:  { float h = Z(4) + (sinf(t*14)*0.5f+0.5f) * Z(9);
                   place_box(m_rect, CX, my, Z(26), h);
                   lv_obj_set_style_radius(m_rect, (int)Z(7), 0);
                   lv_obj_set_style_bg_color(m_rect, c, 0);
                   lv_obj_set_style_opa(m_rect, a, 0); return; }
    case MO_GRIT:  { float bw = Z(36), bh = Z(12);
                   place_box(m_grit, CX, my, bw, bh);
                   lv_obj_set_style_border_color(m_grit, c, 0);
                   lv_obj_set_style_opa(m_grit, a, 0);
                   for (int i = 0; i < 2; i++) {
                       float tx = CX + (i ? Z(6) : -Z(6));
                       pMouth[i*2].x = tx; pMouth[i*2].y = my - Z(6);
                       pMouth[i*2+1].x = tx; pMouth[i*2+1].y = my + Z(6);
                       lv_line_set_points(m_teeth[i], &pMouth[i*2], 2);
                       lv_obj_set_style_line_color(m_teeth[i], c, 0);
                       lv_obj_set_style_opa(m_teeth[i], a, 0);
                   } return; }
    default: return;
    }
    lv_obj_set_style_line_color(m_curve, c, 0);
    lv_obj_set_style_opa(m_curve, a, 0);
}

static void render_extra(float cy, float bh, float lx, float rx, lv_color_t c, lv_opa_t base, float t) {
    float dimf = 1.0f - A.dim;
    switch (cur_extra) {
    case EX_DOTS: for (int i = 0; i < 3; i++) {
            float k = (sinf(t*4 - i*0.8f)+1)/2; place_box(ex_dot[i], CX - Z(18) + i*Z(18), CY - Z(86), Z(9), Z(9));
            lv_obj_set_style_bg_color(ex_dot[i], c, 0); lv_obj_set_style_opa(ex_dot[i], OA(dimf*(0.2f+k*0.8f)), 0); } break;
    case EX_BUSY: for (int i = 0; i < 3; i++) {
            float k = (sinf(t*4 - i*0.7f)+1)/2; place_box(ex_dot[i], CX - Z(22) + i*Z(22), CY + Z(96), Z(10), Z(10));
            lv_obj_set_style_bg_color(ex_dot[i], c, 0); lv_obj_set_style_opa(ex_dot[i], OA(dimf*(0.25f+k*0.75f)), 0); } break;
    case EX_WAVE: for (int i = -1; i <= 1; i++) {
            float hh = Z(8) + (sinf(t*9 + i*1.1f)*0.5f+0.5f)*Z(22); lv_obj_t* o = ex_wave[i+1];
            lv_obj_set_size(o, (int)Z(8), (int)hh); lv_obj_set_pos(o, (int)(CX + i*Z(16) - Z(4)), (int)(CY + Z(110) - hh));
            lv_obj_set_style_radius(o, (int)Z(4), 0); lv_obj_set_style_bg_color(o, c, 0); lv_obj_set_style_opa(o, base, 0); } break;
    case EX_SPARKLE: { const float sp[3][3] = {{-86,-60,1.1f},{92,-30,0.8f},{70,78,1.0f}};
            for (int i = 0; i < 3; i++) { float k = (sinf(t*3 + i*1.7f)+1)/2; float r = sp[i][2]*(Z(3)+k*Z(4));
                place_box(ex_spark[i], CX + Z(sp[i][0]), CY + Z(sp[i][1]), 2*r, 2*r);
                lv_obj_set_style_bg_color(ex_spark[i], c, 0); lv_obj_set_style_opa(ex_spark[i], OA(dimf*k), 0); } } break;
    case EX_GLOBE: { float gx = CX, gy = CY - Z(86), r = Z(11); place_box(ex_globe, gx, gy, 2*r, 2*r);
            lv_obj_set_style_border_color(ex_globe, c, 0); lv_obj_set_style_opa(ex_globe, base, 0);
            float a = t*3; place_box(ex_globe_dot, gx + cosf(a)*r, gy + sinf(a)*Z(4), Z(6), Z(6));
            lv_obj_set_style_bg_color(ex_globe_dot, c, 0); lv_obj_set_style_opa(ex_globe_dot, base, 0); } break;
    case EX_WARN: lv_obj_set_pos(ex_warn, (int)(CX - Z(17)), (int)(CY - Z(104))); lv_obj_set_style_opa(ex_warn, base, 0); break;
    case EX_SWEAT: { float dy = fmodf(t*40, 40); place_box(ex_sweat, rx + Z(34), cy - Z(20) + Z(dy), Z(8), Z(11));
            lv_obj_set_style_bg_color(ex_sweat, lv_color_hex(0x9fe6ff), 0); lv_obj_set_style_opa(ex_sweat, OA(dimf*(1 - dy/40)), 0); } break;
    case EX_EXCL: { float yy = CY - Z(92) + sinf(t*5.5f)*Z(4);
            lv_obj_set_size(ex_excl_bar, (int)Z(10), (int)Z(30)); lv_obj_set_pos(ex_excl_bar, (int)(CX - Z(5)), (int)yy);
            lv_obj_set_style_radius(ex_excl_bar, (int)Z(5), 0); lv_obj_set_style_bg_color(ex_excl_bar, c, 0); lv_obj_set_style_opa(ex_excl_bar, base, 0);
            place_box(ex_excl_dot, CX, yy + Z(44), Z(12), Z(12)); lv_obj_set_style_bg_color(ex_excl_dot, c, 0); lv_obj_set_style_opa(ex_excl_dot, base, 0); } break;
    case EX_PACK: for (int i = 0; i < 8; i++) { float a = t*3 + i*(float)M_PI/4, r = Z(26);
            float k = (sinf(t*3 - i*0.5f)+1)/2; place_box(ex_pack[i], CX + cosf(a)*r, CY + sinf(a)*r, Z(6), Z(6));
            lv_obj_set_style_bg_color(ex_pack[i], c, 0); lv_obj_set_style_opa(ex_pack[i], OA(dimf*(0.25f+0.75f*k)), 0); } break;
    case EX_HEART: { place_box(ex_blush[0], lx, cy + bh/2 + Z(6), Z(18), Z(18));
            place_box(ex_blush[1], rx, cy + bh/2 + Z(6), Z(18), Z(18));
            for (int j = 0; j < 2; j++) { lv_obj_set_style_bg_color(ex_blush[j], lv_color_hex(0xff9ec7), 0); lv_obj_set_style_opa(ex_blush[j], OA(dimf*0.5f), 0); }
            for (int i = 0; i < 3; i++) { float f = fmodf(t*0.45f + i*0.33f, 1.0f);
                float s = Z(8) - f*Z(3); place_box(ex_heart[i], CX + (i-1)*Z(34), CY - Z(50) - f*Z(60), 2*s, 2*s);
                lv_obj_set_style_bg_color(ex_heart[i], lv_color_hex(0xff7ab8), 0); lv_obj_set_style_opa(ex_heart[i], OA(dimf*(1-f)*0.9f), 0); } } break;
    case EX_ZZZ: { const float zz[3][3] = {{78,-30,14},{96,-54,20},{62,-10,12}};
            for (int i = 0; i < 3; i++) { float f = fmodf(t*0.4f + i*0.33f, 1.0f);
                lv_obj_set_pos(ex_zzz[i], (int)(CX + Z(zz[i][0])), (int)(CY + Z(zz[i][1]) - f*Z(16)));
                lv_obj_set_style_text_color(ex_zzz[i], c, 0); lv_obj_set_style_opa(ex_zzz[i], OA(dimf*(1-f)*0.9f), 0); } } break;
    }
}

static void engine_render(void) {
    if (cfg_for != shown) { configure_mood(shown); cfg_for = shown; }
    lv_color_t c = col_now();
    float dimf = 1.0f - A.dim;

    float blinkScale = 1 - sinf(fminf(1.0f, blink) * (float)M_PI) * 0.92f;
    float breathe = sinf(tsec * 1.6f) * 0.012f + 1;
    float bob   = (MOODS[shown].motion & M_BOB)   ? sinf(tsec * 5.5f) * Z(5) : 0;
    float shake = (MOODS[shown].motion & M_SHAKE) ? sinf(tsec * 38) * Z(3) : 0;
    float ease = ssmooth(tphase);

    // PU_GAZE states keep the eye body put and let the pupil do the looking
    // (handled in render_eye); other states still gaze by sliding the whole eye.
    bool gazePupil = (cur_pupil == PU_GAZE);
    float bw = Z(58) * A.eyeW;
    float bh = Z(78) * A.eyeH * blinkScale * breathe * fmaxf(0.06f, ease);
    float gap = Z(70);
    float cy = CY + bob + shake + (gazePupil ? 0 : Z(A.gazeY));
    float lx = CX - gap, rx = CX + gap;
    float shiftX = gazePupil ? 0 : Z(A.gazeX);
    float gxL = lx + shiftX, gxR = rx + shiftX;
    lv_opa_t eyeOpa = OA(dimf);

    render_eye(0, eye_shape_for(cur_shape, 0), gxL, cy, bw, bh, c, eyeOpa, tsec);
    render_eye(1, eye_shape_for(cur_shape, 1), gxR, cy, bw, bh, c, eyeOpa, tsec);
    render_brows(gxL, gxR, cy - bh / 2 - Z(15), c, OA(ease * dimf));
    render_mouth(cy + bh / 2 + Z(30), c, OA(ease * dimf), tsec);
    render_extra(cy, bh, gxL, gxR, c, OA(dimf), tsec);
}

static void tick_cb(lv_timer_t* t) {
    (void)t;
    uint32_t now = millis();
    float dt = last_ms ? (now - last_ms) / 1000.0f : 0.033f;
    last_ms = now;
    engine_tick(dt);
    engine_render();
}

// ─── Mood resolution (host vs sensor override) ───────────────────────────────
static uint32_t ovr_hold(face_state_t s) {
    if (s == FACE_TOUCHED) return 2200;
    if (s == FACE_DIZZY)   return 2800;
    return 1500;
}
static void resolve(void) {
    face_state_t pick = host_state;
    if (ovr_until && PRIO[ovr_state] >= PRIO[host_state]) pick = ovr_state;
    queued = pick;
    if (shown != pick && tdir >= 0) tdir = -1;        // begin blink-changeover
}
void face_set_state(face_state_t s) {
    if (s < 0 || s >= FACE_STATE_COUNT) return;
    host_state = s; resolve();
}
void face_local_override(face_state_t s) {
    if (s < 0 || s >= FACE_STATE_COUNT) return;
    ovr_state = s; ovr_until = millis() + ovr_hold(s);
    if (ovr_until == 0) ovr_until = 1;
    resolve();
}
face_state_t face_get_state(void) { return host_state; }
void face_next_state(void) { face_set_state((face_state_t)((host_state + 1) % FACE_STATE_COUNT)); }
const char* face_state_name(face_state_t s) { return (s >= 0 && s < FACE_STATE_COUNT) ? kNames[s] : "?"; }
bool face_state_from_str(const char* s, face_state_t* out) {
    for (int i = 0; i < FACE_STATE_COUNT; i++)
        if (strcmp(s, kNames[i]) == 0) { *out = (face_state_t)i; return true; }
    return false;
}

// ─── Init / show / hide ──────────────────────────────────────────────────────
void face_init(lv_obj_t* parent) {
    const BoardCaps& caps = board_caps();
    W = caps.width; H = caps.height; CX = W / 2; CY = (int)(H * 0.45f);
    S = (float)W / 284.0f;

    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, W, H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    gen_glow_image();

    for (int i = 0; i < 2; i++) {
        e_body[i] = mk_rect(root);
        e_lid[i]   = mk_rect(e_body[i]);            // child: clipped to eye
        e_pupil[i] = mk_circle(e_body[i]);
        e_pupil2[i] = mk_circle(e_body[i]);
        lv_obj_set_style_radius(e_body[i], (int)Z(24), 0);
        e_curve[i]  = mk_line(root, Z(11));
        e_sq[i][0]  = mk_line(root, Z(10));
        e_sq[i][1]  = mk_line(root, Z(10));
        e_spiral[i] = mk_line(root, Z(6));
        brow[i]     = mk_line(root, Z(7));
    }

    m_curve = mk_line(root, Z(7));
    m_o = mk_circle(root);
    lv_obj_set_style_bg_opa(m_o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_o, (int)Z(5), 0);
    lv_obj_set_style_border_opa(m_o, LV_OPA_COVER, 0);
    m_rect = mk_rect(root);
    m_grit = mk_rect(root);
    lv_obj_set_style_bg_opa(m_grit, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(m_grit, (int)Z(4), 0);
    lv_obj_set_style_border_opa(m_grit, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(m_grit, (int)Z(4), 0);
    m_teeth[0] = mk_line(root, Z(4)); m_teeth[1] = mk_line(root, Z(4));

    for (int i = 0; i < 3; i++) ex_dot[i]   = mk_circle(root);
    for (int i = 0; i < 3; i++) ex_spark[i] = mk_circle(root);
    for (int i = 0; i < 8; i++) ex_pack[i]  = mk_circle(root);
    ex_globe = mk_circle(root);
    lv_obj_set_style_bg_opa(ex_globe, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ex_globe, (int)Z(2.5f), 0);
    lv_obj_set_style_border_opa(ex_globe, LV_OPA_COVER, 0);
    ex_globe_dot = mk_circle(root);
    gen_warn_image();
    ex_sweat = mk_circle(root);
    ex_excl_bar = mk_rect(root);
    ex_excl_dot = mk_circle(root);
    for (int i = 0; i < 2; i++) ex_blush[i] = mk_circle(root);
    for (int i = 0; i < 3; i++) ex_heart[i] = mk_circle(root);
    for (int i = 0; i < 3; i++) {
        ex_zzz[i] = lv_label_create(root);
        lv_label_set_text(ex_zzz[i], "z");
        lv_obj_set_style_text_font(ex_zzz[i], &font_styrene_20, 0);
        lv_obj_add_flag(ex_zzz[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 3; i++) ex_wave[i] = mk_rect(root);

    // Status line under the face.
    status_lbl = lv_label_create(root);
    lv_obj_set_style_text_font(status_lbl, &font_zh_22, 0);
    lv_obj_set_width(status_lbl, W);
    lv_obj_set_style_text_align(status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(status_lbl, 3, 0);
    lv_obj_set_style_opa(status_lbl, LV_OPA_90, 0);
    lv_obj_align(status_lbl, LV_ALIGN_BOTTOM_MID, 0, -36);
    lv_label_set_text(status_lbl, "待命");

    // Transparent top catcher so a tap bubbles to root's click handler.
    lv_obj_t* tcatch = lv_obj_create(root);
    lv_obj_remove_style_all(tcatch);
    lv_obj_set_size(tcatch, W, H);
    lv_obj_set_pos(tcatch, 0, 0);
    lv_obj_set_style_bg_opa(tcatch, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(tcatch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(tcatch, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(tcatch, LV_OBJ_FLAG_SCROLLABLE);

    tmr = lv_timer_create(tick_cb, 33, NULL);
    lv_timer_pause(tmr);

    cfg_for = (face_state_t)-1;
    last_ms = 0;
    face_set_state(FACE_IDLE);
    configure_mood(FACE_IDLE); cfg_for = FACE_IDLE;
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
}

void face_show(void) {
    if (root) lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    if (tmr)  { last_ms = 0; lv_timer_resume(tmr); }
}
void face_hide(void) {
    if (root) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    if (tmr)  lv_timer_pause(tmr);
}
lv_obj_t* face_get_root(void) { return root; }
