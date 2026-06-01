#include "face.h"
#include "theme.h"
#include "hal/board_caps.h"
#include <lvgl.h>
#include <math.h>
#include <string.h>

LV_FONT_DECLARE(font_styrene_20);

// ─── Geometry (368x448, AMOLED-1.8; 1cqw=3.68px, 1cqh=4.48px) ────────────
#define EYE_W        62
#define EYE_H        96
#define EYE_GAP      48
#define EYE_RADIUS   30
#define EYE_MIN_H    6
#define SHINE_W      18
#define SHINE_H      22
#define BROW_W       58
#define BROW_TH      12
#define MOUTH_W      40
#define BOB_AMP      6

// Layout, filled in face_init from board size.
static int W, H, CX;
static int eye_cy;          // eye vertical centre (inside bob)
static int eye_base_y;      // eye top when fully open
static int left_x, right_x; // eye left positions
static int brow_y;          // brow object y
static int mouth_cy;        // mouth vertical centre

// ─── Parts ───────────────────────────────────────────────────────────────
static lv_obj_t* root  = nullptr;
static lv_obj_t* bob   = nullptr;
static lv_obj_t* eye_l = nullptr;
static lv_obj_t* eye_r = nullptr;
static lv_obj_t* brow_l = nullptr;
static lv_obj_t* brow_r = nullptr;
static lv_obj_t* mouth = nullptr;
static lv_obj_t* happy_l = nullptr;
static lv_obj_t* happy_r = nullptr;
static lv_obj_t* crossL[2] = {nullptr};  // two lines forming a ╳ over left eye
static lv_obj_t* crossR[2] = {nullptr};
static lv_obj_t* ring  = nullptr;
static lv_obj_t* dots[3] = {nullptr};
static lv_obj_t* sparks[5] = {nullptr};
static lv_obj_t* name_lbl = nullptr;   // current state name (bottom)

// Two layers only: every layer is re-alpha-blended wherever a moving element
// (eyes, ring, sway) passes over it, so layer count directly taxes the heavy
// animation states. Two reads as a soft halo while halving that overdraw.
#define GLOW_LAYERS 2
static lv_obj_t* glow[GLOW_LAYERS] = {nullptr};
static const lv_opa_t glow_base[GLOW_LAYERS] = {18, 30};

// Persistent point arrays (lv_line keeps the pointer, not a copy).
static lv_point_precise_t browL_pts[2], browR_pts[2];
static lv_point_precise_t crL1[2], crL2[2], crR1[2], crR2[2];
static lv_point_precise_t dotsBaseY[3];  // unused placeholder kept tidy

// Per-state runtime values shared with exec callbacks.
static face_state_t cur_state = FACE_IDLE;
static int   eye_open_h = EYE_H;   // current open eye height (varies per state)
static int   eye_open_w = EYE_W;
static float eye_scale  = 1.0f;    // waiting enlarges
static int   blink_last_h = -1;

// ─── State colours ────────────────────────────────────────────────────────
static lv_color_t state_color(face_state_t s) {
    switch (s) {
    case FACE_IDLE:     return THEME_STATE_IDLE;
    case FACE_THINKING: return THEME_STATE_THINKING;
    case FACE_WORKING:  return THEME_STATE_WORKING;
    case FACE_WAITING:  return THEME_STATE_WAITING;
    case FACE_SPEAKING: return THEME_STATE_SPEAKING;
    case FACE_DONE:     return THEME_STATE_DONE;
    case FACE_ERROR:    return THEME_STATE_ERROR;
    default:            return THEME_STATE_IDLE;
    }
}

// ─── Builders ──────────────────────────────────────────────────────────────
static void make_glow(lv_obj_t* parent, int cx, int cy) {
    const int d[GLOW_LAYERS] = {190, 110};
    for (int i = 0; i < GLOW_LAYERS; i++) {
        lv_obj_t* g = lv_obj_create(parent);
        lv_obj_remove_style_all(g);
        lv_obj_set_size(g, d[i], d[i]);
        lv_obj_set_pos(g, cx - d[i] / 2, cy - d[i] / 2);
        lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(g, glow_base[i], 0);
        lv_obj_clear_flag(g, LV_OBJ_FLAG_SCROLLABLE);
        glow[i] = g;
    }
}

static lv_obj_t* make_eye(lv_obj_t* parent, int x, int y) {
    lv_obj_t* eye = lv_obj_create(parent);
    lv_obj_remove_style_all(eye);
    lv_obj_set_size(eye, EYE_W, EYE_H);
    lv_obj_set_pos(eye, x, y);
    lv_obj_set_style_bg_opa(eye, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye, EYE_RADIUS, 0);
    lv_obj_clear_flag(eye, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* shine = lv_obj_create(eye);
    lv_obj_remove_style_all(shine);
    lv_obj_set_size(shine, SHINE_W, SHINE_H);
    lv_obj_set_pos(shine, (int)(EYE_W * 0.20f), (int)(EYE_H * 0.16f));
    lv_obj_set_style_bg_color(shine, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(shine, LV_OPA_90, 0);
    lv_obj_set_style_radius(shine, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(shine, LV_OBJ_FLAG_SCROLLABLE);
    return eye;
}

static lv_obj_t* make_brow(lv_obj_t* parent, int x, lv_point_precise_t* pts) {
    lv_obj_t* b = lv_line_create(parent);
    lv_obj_set_pos(b, x, brow_y);
    lv_obj_set_style_line_width(b, BROW_TH, 0);
    lv_obj_set_style_line_rounded(b, true, 0);
    pts[0].x = 0;      pts[0].y = 0;
    pts[1].x = BROW_W; pts[1].y = 0;
    lv_line_set_points(b, pts, 2);
    lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
    return b;
}

// Two crossed lines (╳) sized box×box centred on an eye centre.
static void make_cross(lv_obj_t* parent, int cx, int cy, lv_obj_t** out,
                       lv_point_precise_t* p1, lv_point_precise_t* p2) {
    const int box = 50;
    for (int k = 0; k < 2; k++) {
        lv_obj_t* ln = lv_line_create(parent);
        lv_obj_set_pos(ln, cx - box / 2, cy - box / 2);
        lv_obj_set_style_line_width(ln, 12, 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_add_flag(ln, LV_OBJ_FLAG_HIDDEN);
        out[k] = ln;
    }
    p1[0].x = 0;   p1[0].y = 0;   p1[1].x = box; p1[1].y = box;   // ╲
    p2[0].x = box; p2[0].y = 0;   p2[1].x = 0;   p2[1].y = box;   // ╱
    lv_line_set_points(out[0], p1, 2);
    lv_line_set_points(out[1], p2, 2);
}

// Upper-semicircle arc (∩) for a happy eye.
static lv_obj_t* make_happy(lv_obj_t* parent, int cx, int cy) {
    const int d = 56;
    lv_obj_t* a = lv_arc_create(parent);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, cx - d / 2, cy - d / 2);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);     // hide track
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_KNOB);      // hide knob
    lv_obj_set_style_arc_width(a, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    lv_arc_set_rotation(a, 0);
    lv_arc_set_bg_angles(a, 180, 360);
    lv_arc_set_angles(a, 180, 360);   // top half ∩
    lv_obj_add_flag(a, LV_OBJ_FLAG_HIDDEN);
    return a;
}

// ─── Animation exec callbacks ──────────────────────────────────────────────
static void anim_bob_y(void* v, int32_t y)  { lv_obj_set_y((lv_obj_t*)v, y); }

static int eye_dx = 0;   // horizontal look/sway offset (eyes only — cheap)

static void apply_eye_size(int h) {
    int w = eye_open_w;
    int xoff = (EYE_W - w) / 2 + eye_dx;
    int yoff = (EYE_H - h) / 2;   // keep centred in the EYE_W×EYE_H slot
    if (eye_l) { lv_obj_set_size(eye_l, w, h); lv_obj_set_pos(eye_l, left_x  + xoff, eye_base_y + yoff); }
    if (eye_r) { lv_obj_set_size(eye_r, w, h); lv_obj_set_pos(eye_r, right_x + xoff, eye_base_y + yoff); }
}

// Look / sway: shift only the eyes (a small dirty area) instead of the whole
// face — moving the whole bob re-flushes the entire face region per frame and
// is QSPI-bound on this panel.
static void anim_eye_dx(void* v, int32_t x) {
    (void)v;
    eye_dx = x;
    blink_last_h = -1;          // force apply (height may be unchanged)
    apply_eye_size(eye_open_h);
}

// Blink: 0..1000 phase, short squish near the top of the cycle.
static void anim_blink(void* v, int32_t p) {
    (void)v;
    float s = 0.0f;
    if (p >= 930) { s = (p <= 965) ? (p - 930) / 35.0f : (1000 - p) / 35.0f; }
    if (s < 0) s = 0; if (s > 1) s = 1;
    int h = eye_open_h - (int)(s * (eye_open_h - EYE_MIN_H));
    if (h == blink_last_h) return;
    blink_last_h = h;
    apply_eye_size(h);
}

// Working: horizontal pulse of eye width (scaleX 1.0..1.14).
static void anim_eye_wpulse(void* v, int32_t p) {
    (void)v;
    float f = 1.0f + 0.14f * (p / 1000.0f);
    eye_open_w = (int)(EYE_W * f);
    apply_eye_size(eye_open_h);
}

static void anim_ring_spin(void* v, int32_t deg) { lv_arc_set_rotation((lv_obj_t*)v, deg); }

static void anim_dot_y(void* v, int32_t y) { lv_obj_set_y((lv_obj_t*)v, y); }

// Mouth open/close: height between min and max, kept centred at mouth_cy.
static void anim_mouth_h(void* v, int32_t h) {
    lv_obj_t* m = (lv_obj_t*)v;
    lv_obj_set_height(m, h);
    lv_obj_set_y(m, mouth_cy - h / 2);
}

static void anim_spark(void* v, int32_t p) {
    // p 0..1000 → twinkle opacity
    lv_opa_t o = (lv_opa_t)(p < 350 ? (p / 350.0f) * 255
                          : p < 700 ? (1 - (p - 350) / 350.0f) * 255 : 0);
    lv_obj_set_style_bg_opa((lv_obj_t*)v, o, 0);
}

// ─── Reset & helpers ───────────────────────────────────────────────────────
static void clear_anims(void) {
    lv_anim_delete(bob, NULL);
    lv_anim_delete(eye_l, NULL);
    lv_anim_delete(eye_r, NULL);
    lv_anim_delete(mouth, NULL);
    lv_anim_delete(ring, NULL);
    for (int i = 0; i < 3; i++) lv_anim_delete(dots[i], NULL);
    for (int i = 0; i < 5; i++) lv_anim_delete(sparks[i], NULL);
    lv_anim_delete(glow[0], NULL);
}

static void set_brow(lv_obj_t* b, lv_point_precise_t* pts, int x, float tilt_deg, int y_extra) {
    float t = tilt_deg * 3.14159265f / 180.0f;
    int dy = (int)((BROW_W / 2) * sinf(t));
    pts[0].x = 0;      pts[0].y = -dy;     // left end
    pts[1].x = BROW_W; pts[1].y =  dy;     // right end (positive tilt → right down)
    lv_line_set_points(b, pts, 2);
    lv_obj_set_pos(b, x, brow_y + y_extra);
}

static void show(lv_obj_t* o, bool v) {
    if (!o) return;
    if (v) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    else   lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

static void recolor(lv_color_t c) {
    if (eye_l) lv_obj_set_style_bg_color(eye_l, c, 0);
    if (eye_r) lv_obj_set_style_bg_color(eye_r, c, 0);
    for (int i = 0; i < GLOW_LAYERS; i++)
        if (glow[i]) lv_obj_set_style_bg_color(glow[i], c, 0);
    if (brow_l) lv_obj_set_style_line_color(brow_l, c, 0);
    if (brow_r) lv_obj_set_style_line_color(brow_r, c, 0);
    if (mouth)  lv_obj_set_style_bg_color(mouth, c, 0);
    if (happy_l) lv_obj_set_style_arc_color(happy_l, c, LV_PART_INDICATOR);
    if (happy_r) lv_obj_set_style_arc_color(happy_r, c, LV_PART_INDICATOR);
    for (int k = 0; k < 2; k++) {
        if (crossL[k]) lv_obj_set_style_line_color(crossL[k], c, 0);
        if (crossR[k]) lv_obj_set_style_line_color(crossR[k], c, 0);
    }
    if (ring) lv_obj_set_style_arc_color(ring, c, LV_PART_INDICATOR);
    for (int i = 0; i < 3; i++) if (dots[i]) lv_obj_set_style_bg_color(dots[i], c, 0);
    for (int i = 0; i < 5; i++) if (sparks[i]) lv_obj_set_style_bg_color(sparks[i], c, 0);
}

// Hide every optional part and restore neutral pose. Each state then opts in.
static void reset_parts(void) {
    clear_anims();
    lv_obj_set_pos(bob, 0, 0);
    eye_open_h = EYE_H; eye_open_w = EYE_W; eye_scale = 1.0f; blink_last_h = -1; eye_dx = 0;
    show(eye_l, true); show(eye_r, true);
    apply_eye_size(EYE_H);
    show(brow_l, false); show(brow_r, false);
    show(mouth, false);
    show(happy_l, false); show(happy_r, false);
    for (int k = 0; k < 2; k++) { show(crossL[k], false); show(crossR[k], false); }
    show(ring, false);
    for (int i = 0; i < 3; i++) show(dots[i], false);
    for (int i = 0; i < 5; i++) { show(sparks[i], false); lv_obj_set_style_bg_opa(sparks[i], LV_OPA_TRANSP, 0); }
    for (int i = 0; i < GLOW_LAYERS; i++) lv_obj_set_style_bg_opa(glow[i], glow_base[i], 0);
}

// Convenience anim starters.
static void start_bob_y(void) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, bob); lv_anim_set_exec_cb(&a, anim_bob_y);
    lv_anim_set_values(&a, -BOB_AMP, BOB_AMP);
    lv_anim_set_duration(&a, 2500); lv_anim_set_reverse_duration(&a, 2500);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}
static void start_blink(uint32_t period) {
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, eye_l); lv_anim_set_exec_cb(&a, anim_blink);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, period);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

// ─── Public: state machine ─────────────────────────────────────────────────
void face_set_state(face_state_t s) {
    if (s < 0 || s >= FACE_STATE_COUNT) return;
    cur_state = s;
    reset_parts();
    recolor(state_color(s));

    if (name_lbl) {
        char up[16];
        const char* n = face_state_name(s);
        size_t i = 0;
        for (; n[i] && i < sizeof(up) - 1; i++)
            up[i] = (n[i] >= 'a' && n[i] <= 'z') ? n[i] - 32 : n[i];
        up[i] = '\0';
        lv_label_set_text(name_lbl, up);
        lv_obj_set_style_text_color(name_lbl, state_color(s), 0);
    }

    switch (s) {
    case FACE_IDLE:
        start_bob_y();
        start_blink(5000);
        break;

    case FACE_THINKING: {
        start_bob_y();
        start_blink(4000);
        // look left-right (eyes only)
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, eye_r); lv_anim_set_exec_cb(&a, anim_eye_dx);
        lv_anim_set_values(&a, -10, 10);
        lv_anim_set_duration(&a, 1500); lv_anim_set_reverse_duration(&a, 1500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
        // brows: slight inward
        show(brow_l, true); show(brow_r, true);
        set_brow(brow_l, browL_pts, left_x + 2,  10, 0);
        set_brow(brow_r, browR_pts, right_x + 2, -10, 0);
        // thinking dots jump
        for (int i = 0; i < 3; i++) {
            show(dots[i], true);
            lv_anim_t d; lv_anim_init(&d);
            lv_anim_set_var(&d, dots[i]); lv_anim_set_exec_cb(&d, anim_dot_y);
            int by = mouth_cy + 40;
            lv_anim_set_values(&d, by, by - 14);
            lv_anim_set_duration(&d, 360); lv_anim_set_reverse_duration(&d, 360);
            lv_anim_set_repeat_count(&d, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_repeat_delay(&d, 720);
            lv_anim_set_delay(&d, i * 180);
            lv_anim_set_path_cb(&d, lv_anim_path_ease_in_out);
            lv_anim_start(&d);
        }
        break;
    }

    case FACE_WORKING: {
        eye_open_h = 58;            // focused, squinted
        apply_eye_size(eye_open_h);
        start_bob_y();
        // horizontal eye pulse
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, eye_r); lv_anim_set_exec_cb(&a, anim_eye_wpulse);
        lv_anim_set_values(&a, 0, 1000);
        lv_anim_set_duration(&a, 650); lv_anim_set_reverse_duration(&a, 650);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
        // spinning ring
        show(ring, true);
        lv_anim_t r; lv_anim_init(&r);
        lv_anim_set_var(&r, ring); lv_anim_set_exec_cb(&r, anim_ring_spin);
        lv_anim_set_values(&r, 0, 3600);
        lv_anim_set_duration(&r, 1400);
        lv_anim_set_repeat_count(&r, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&r, lv_anim_path_linear);
        lv_anim_start(&r);
        break;
    }

    case FACE_WAITING: {
        eye_open_h = (int)(EYE_H * 1.10f);
        eye_open_w = (int)(EYE_W * 1.10f);
        apply_eye_size(eye_open_h);
        start_bob_y();
        start_blink(3000);
        // agitated side-to-side (eyes only, brisk — attention)
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, eye_r); lv_anim_set_exec_cb(&a, anim_eye_dx);
        lv_anim_set_values(&a, -9, 9);
        lv_anim_set_duration(&a, 320); lv_anim_set_reverse_duration(&a, 320);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_start(&a);
        // brows raised/outward
        show(brow_l, true); show(brow_r, true);
        set_brow(brow_l, browL_pts, left_x + 2,  -14, -4);
        set_brow(brow_r, browR_pts, right_x + 2,  14, -4);
        // mouth open/close (calling out)
        show(mouth, true);
        lv_obj_set_style_radius(mouth, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_width(mouth, 34);
        lv_anim_t m; lv_anim_init(&m);
        lv_anim_set_var(&m, mouth); lv_anim_set_exec_cb(&m, anim_mouth_h);
        lv_anim_set_values(&m, 14, 30);
        lv_anim_set_duration(&m, 350); lv_anim_set_reverse_duration(&m, 350);
        lv_anim_set_repeat_count(&m, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&m, lv_anim_path_ease_in_out);
        lv_anim_start(&m);
        break;
    }

    case FACE_SPEAKING: {
        eye_open_h = (int)(EYE_H * 0.80f);
        apply_eye_size(eye_open_h);
        start_bob_y();
        start_blink(3500);
        // fast talking mouth
        show(mouth, true);
        lv_obj_set_style_radius(mouth, 14, 0);
        lv_obj_set_width(mouth, 44);
        lv_anim_t m; lv_anim_init(&m);
        lv_anim_set_var(&m, mouth); lv_anim_set_exec_cb(&m, anim_mouth_h);
        lv_anim_set_values(&m, 10, 44);
        lv_anim_set_duration(&m, 170); lv_anim_set_reverse_duration(&m, 170);
        lv_anim_set_repeat_count(&m, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&m, lv_anim_path_ease_in_out);
        lv_anim_start(&m);
        break;
    }

    case FACE_DONE: {
        start_bob_y();
        // happy eyes instead of lids
        show(eye_l, false); show(eye_r, false);
        show(happy_l, true); show(happy_r, true);
        // smile (downward-bulge arc): a wide rounded rect, bottom corners round
        show(mouth, true);
        lv_obj_set_size(mouth, 52, 26);
        lv_obj_set_style_radius(mouth, 0, 0);
        lv_obj_set_style_radius(mouth, 26, 0);  // pill-ish
        lv_obj_set_pos(mouth, CX - 26, mouth_cy - 4);
        // sparks twinkle
        for (int i = 0; i < 5; i++) {
            show(sparks[i], true);
            lv_anim_t sp; lv_anim_init(&sp);
            lv_anim_set_var(&sp, sparks[i]); lv_anim_set_exec_cb(&sp, anim_spark);
            lv_anim_set_values(&sp, 0, 1000);
            lv_anim_set_duration(&sp, 1600);
            lv_anim_set_repeat_count(&sp, LV_ANIM_REPEAT_INFINITE);
            lv_anim_set_delay(&sp, i * 250);
            lv_anim_set_path_cb(&sp, lv_anim_path_linear);
            lv_anim_start(&sp);
        }
        break;
    }

    case FACE_ERROR: {
        // static droop (no float), crosses for eyes
        lv_obj_set_pos(bob, 0, 8);
        show(eye_l, false); show(eye_r, false);
        for (int k = 0; k < 2; k++) { show(crossL[k], true); show(crossR[k], true); }
        // drooping brows (八字)
        show(brow_l, true); show(brow_r, true);
        set_brow(brow_l, browL_pts, left_x + 2,  18, 6);
        set_brow(brow_r, browR_pts, right_x + 2, -18, 6);
        // sad mouth: small frown (upward-bulge). Use a short pill low down.
        show(mouth, true);
        lv_obj_set_size(mouth, 40, 14);
        lv_obj_set_style_radius(mouth, 8, 0);
        lv_obj_set_pos(mouth, CX - 20, mouth_cy + 6);
        break;
    }
    default: break;
    }
}

face_state_t face_get_state(void) { return cur_state; }

void face_next_state(void) {
    face_set_state((face_state_t)((cur_state + 1) % FACE_STATE_COUNT));
}

static const char* kNames[FACE_STATE_COUNT] = {
    "idle", "thinking", "working", "waiting", "speaking", "done", "error"
};
const char* face_state_name(face_state_t s) {
    return (s >= 0 && s < FACE_STATE_COUNT) ? kNames[s] : "?";
}
bool face_state_from_str(const char* s, face_state_t* out) {
    for (int i = 0; i < FACE_STATE_COUNT; i++)
        if (strcmp(s, kNames[i]) == 0) { *out = (face_state_t)i; return true; }
    return false;
}

// ─── Init ──────────────────────────────────────────────────────────────────
void face_init(lv_obj_t* parent) {
    const BoardCaps& caps = board_caps();
    W = caps.width; H = caps.height; CX = W / 2;
    eye_cy = (int)(H * 0.43f);
    eye_base_y = eye_cy - EYE_H / 2;
    left_x  = CX - EYE_GAP / 2 - EYE_W;
    right_x = CX + EYE_GAP / 2;
    brow_y  = eye_base_y - 22;
    mouth_cy = eye_cy + 74;

    root = lv_obj_create(parent);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, W, H);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    make_glow(root, CX, (int)(H * 0.42f));

    // Ring (fixed, behind face).
    ring = lv_arc_create(root);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    int rd = 200;   // smaller bbox → cheaper per-frame redraw while spinning
    lv_obj_set_size(ring, rd, rd);
    lv_obj_set_pos(ring, CX - rd / 2, eye_cy - rd / 2);
    lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_angles(ring, 0, 70);
    lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);

    // Sparks (fixed positions around face).
    const int spx[5] = {70, 280, 300, 60, 184};
    const int spy[5] = {120, 110, 250, 250, 80};
    for (int i = 0; i < 5; i++) {
        lv_obj_t* s = lv_obj_create(root);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 12, 12);
        lv_obj_set_pos(s, spx[i], spy[i]);
        lv_obj_set_style_radius(s, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s, LV_OPA_TRANSP, 0);
        lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
        sparks[i] = s;
    }

    // Floating layer.
    bob = lv_obj_create(root);
    lv_obj_remove_style_all(bob);
    lv_obj_set_size(bob, W, H);
    lv_obj_set_pos(bob, 0, 0);
    lv_obj_clear_flag(bob, LV_OBJ_FLAG_SCROLLABLE);

    brow_l = make_brow(bob, left_x + 2,  browL_pts);
    brow_r = make_brow(bob, right_x + 2, browR_pts);
    eye_l = make_eye(bob, left_x,  eye_base_y);
    eye_r = make_eye(bob, right_x, eye_base_y);
    happy_l = make_happy(bob, left_x + EYE_W / 2,  eye_cy);
    happy_r = make_happy(bob, right_x + EYE_W / 2, eye_cy);
    make_cross(bob, left_x + EYE_W / 2,  eye_cy, crossL, crL1, crL2);
    make_cross(bob, right_x + EYE_W / 2, eye_cy, crossR, crR1, crR2);

    mouth = lv_obj_create(bob);
    lv_obj_remove_style_all(mouth);
    lv_obj_set_size(mouth, MOUTH_W, 16);
    lv_obj_set_pos(mouth, CX - MOUTH_W / 2, mouth_cy - 8);
    lv_obj_set_style_bg_opa(mouth, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(mouth, 8, 0);
    lv_obj_add_flag(mouth, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(mouth, LV_OBJ_FLAG_SCROLLABLE);

    // Thinking dots.
    for (int i = 0; i < 3; i++) {
        lv_obj_t* d = lv_obj_create(bob);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 16, 16);
        lv_obj_set_pos(d, CX - 28 + i * 28, mouth_cy + 40);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_SCROLLABLE);
        dots[i] = d;
        (void)dotsBaseY;
    }

    // State-name label (fixed at the bottom, not part of the floating face).
    name_lbl = lv_label_create(root);
    lv_label_set_text(name_lbl, "IDLE");
    lv_obj_set_style_text_font(name_lbl, &font_styrene_20, 0);
    lv_obj_set_style_text_opa(name_lbl, LV_OPA_60, 0);
    lv_obj_set_style_text_letter_space(name_lbl, 4, 0);
    lv_obj_align(name_lbl, LV_ALIGN_BOTTOM_MID, 0, -56);

    // Transparent top-most catcher so a tap anywhere bubbles to root's
    // click handler (children would otherwise swallow the press).
    lv_obj_t* touch = lv_obj_create(root);
    lv_obj_remove_style_all(touch);
    lv_obj_set_size(touch, W, H);
    lv_obj_set_pos(touch, 0, 0);
    lv_obj_set_style_bg_opa(touch, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(touch, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(touch, LV_OBJ_FLAG_SCROLLABLE);

    face_set_state(FACE_IDLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);  // shown via face_show()
}

void face_show(void) { if (root) lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN); }
void face_hide(void) { if (root) lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN); }
lv_obj_t* face_get_root(void) { return root; }
