#pragma once
#include <lvgl.h>

// Design tokens — single source of truth for UI colors. Anthropic-inspired
// dark palette, AMOLED-friendly (true black bg).
#define THEME_BG       lv_color_hex(0x000000)   // screen background
#define THEME_PANEL    lv_color_hex(0x1f1f1e)   // card/zone fill
#define THEME_TEXT     lv_color_hex(0xfaf9f5)   // primary text
#define THEME_DIM      lv_color_hex(0xb0aea5)   // secondary text
#define THEME_ACCENT   lv_color_hex(0xd97757)   // brand terra-cotta
#define THEME_GREEN    lv_color_hex(0x788c5d)
#define THEME_AMBER    lv_color_hex(0xd97757)
#define THEME_RED      lv_color_hex(0xc0392b)
#define THEME_BAR_BG   lv_color_hex(0x2a2a28)   // unfilled bar track

// Desk-buddy expression state colors — one theme color per face state.
// Drives eyes / glow / status text. Source: docs/UI_SPEC.md & UI_PREVIEW.html.
#define THEME_STATE_IDLE      lv_color_hex(0xcdd6f4)   // cool white
#define THEME_STATE_THINKING  lv_color_hex(0x7dd3fc)   // cyan
#define THEME_STATE_WORKING   lv_color_hex(0x5b9dff)   // blue
#define THEME_STATE_WAITING   lv_color_hex(0xf5b53d)   // amber
#define THEME_STATE_SPEAKING  lv_color_hex(0xc08bf0)   // purple
#define THEME_STATE_DONE      lv_color_hex(0x46d68a)   // green
#define THEME_STATE_ERROR     lv_color_hex(0xf06a5e)   // red
