#pragma once
#include <lvgl.h>

// Desk-buddy expression face — the companion's primary screen. Built from
// LVGL vector primitives (rounded-rect eyes, arcs, lines, shine dots) and
// lv_anim tweens, never sprite sheets, per docs/UI_SPEC.md. Animation
// behaviour is canonical in docs/UI_PREVIEW.html.

enum face_state_t {
    FACE_IDLE = 0,
    FACE_THINKING,
    FACE_WORKING,
    FACE_WAITING,
    FACE_SPEAKING,
    FACE_DONE,
    FACE_ERROR,
    FACE_STATE_COUNT,
};

// Create the face widgets inside `parent` (typically lv_screen_active()).
// Starts in FACE_IDLE, hidden — call face_show().
void face_init(lv_obj_t* parent);

// Switch expression. Reconfigures parts (colour, visibility) and restarts
// the per-state animations with a short cross-state settle.
void face_set_state(face_state_t s);
face_state_t face_get_state(void);

// Advance to the next expression (wraps). Used by tap-to-cycle review.
void face_next_state(void);

// Map a state string ("idle", "thinking", …) to a state. Returns false if
// the name is unknown (out unchanged).
bool face_state_from_str(const char* s, face_state_t* out);
const char* face_state_name(face_state_t s);

// Show / hide the face container.
void face_show(void);
void face_hide(void);

// Root container, so ui.cpp can attach a click/cycle event.
lv_obj_t* face_get_root(void);
