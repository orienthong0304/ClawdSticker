#pragma once
#include <lvgl.h>

// Desk-buddy "machine eye" expression engine (clawpet v2). A single full-screen
// lv_canvas in PSRAM is redrawn each frame by an immediate-mode software
// renderer that ports docs/v2/clawpet-face.html 1:1: per-state eye shape + brow
// + pupil + mouth + ambient extra, "blink-changeover" transitions (shape swaps
// hide inside a blink), and idle micro-motions (blink / saccade / breathe).
// Never sprite sheets. Visual reference is canonical in docs/v2/clawpet-face.html.
//
// Only a tight dirty bounding box is invalidated per frame (full screen during
// the brief transition/colour-ease settle window) — the panel is QSPI-bound
// with no TE sync, so flush area, not redraw cost, governs tearing.

enum face_state_t {
    FACE_IDLE = 0,     // awake / calm
    FACE_LISTENING,
    FACE_THINKING,
    FACE_WORKING,
    FACE_SEARCHING,
    FACE_BROWSING,
    FACE_DANGER,
    FACE_PERMISSION,
    FACE_DENIED,
    FACE_BORED,
    FACE_AUTHOK,
    FACE_DONE,
    FACE_SUBDONE,
    FACE_ERROR,
    FACE_RATE,
    FACE_COMPACTING,
    FACE_TOUCHED,
    FACE_DIZZY,
    FACE_SLEEP,
    FACE_SPEAKING,
    FACE_STATE_COUNT,
};

// Build the canvas inside `parent` (typically lv_screen_active()). Starts in
// FACE_IDLE, hidden — call face_show().
void face_init(lv_obj_t* parent);

// Host-driven mood (BLE state string / serial `face` command).
void face_set_state(face_state_t s);
face_state_t face_get_state(void);

// Advance to the next mood (wraps). Dev review cycle (serial / optional tap).
void face_next_state(void);

// Board-local sensor mood (touched / dizzy). Shows `s` for its default hold,
// overriding the host mood only if its priority is higher, then falls back to
// the host mood automatically. See docs/v2 §4.
void face_local_override(face_state_t s);

// Map a state string ("idle", "searching", "danger", …) to a state. Returns
// false if unknown (out unchanged).
bool face_state_from_str(const char* s, face_state_t* out);
const char* face_state_name(face_state_t s);

void face_show(void);
void face_hide(void);
lv_obj_t* face_get_root(void);
