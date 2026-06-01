#include "../../hal/display_hal.h"
#include "board.h"
#include "io_expander.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Preferences.h>

// AMOLED-1.8 panel has no usable hardware rotation, so 180° (for upside-down
// desk mounting) is done in software: a 180° rotation of a contiguous w×h block
// is just a full reversal of its linear pixel buffer, drawn at the mirror-image
// position. Touch coords are flipped in touch.cpp. The setting persists in NVS
// and is restored on boot. Display reset is routed through the XCA9554 IO
// expander (EXIO1), released before gfx->begin() runs.

static Arduino_DataBus* bus = nullptr;
static Arduino_SH8601*  gfx = nullptr;
static bool s_flip180 = false;
static Preferences s_prefs;

void display_hal_init(void) {
    s_prefs.begin("deskbuddy", true);                 // read-only
    s_flip180 = s_prefs.getBool("flip180", false);
    s_prefs.end();

    bus = new Arduino_ESP32QSPI(
        LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
    // SH8601 constructor: (bus, rst, rotation, w, h)
    gfx = new Arduino_SH8601(
        bus, GFX_NOT_DEFINED /* reset via XCA9554 */, 0,
        LCD_WIDTH, LCD_HEIGHT);
}

void display_hal_begin(void) {
    gfx->begin();
    gfx->fillScreen(0x0000);
    gfx->setBrightness(200);
}

void display_hal_set_brightness(uint8_t level) {
    if (gfx) gfx->setBrightness(level);
}

void display_hal_fill_screen(uint16_t color) {
    if (gfx) gfx->fillScreen(color);
}

void display_hal_draw_bitmap(int32_t x, int32_t y, int32_t w, int32_t h,
                             const uint16_t* pixels) {
    if (!gfx) return;
    if (s_flip180) {
        // 180° = reverse the linear buffer in place, draw at the mirror corner.
        uint16_t* p = (uint16_t*)pixels;
        for (int32_t i = 0, j = w * h - 1; i < j; i++, j--) {
            uint16_t t = p[i]; p[i] = p[j]; p[j] = t;
        }
        gfx->draw16bitRGBBitmap(LCD_WIDTH - x - w, LCD_HEIGHT - y - h, p, w, h);
    } else {
        gfx->draw16bitRGBBitmap(x, y, (uint16_t*)pixels, w, h);
    }
}

void display_hal_tick(void) {
    // No automatic (IMU) rotation on this board; flip is user-toggled.
}

void display_hal_set_flip180(bool on) {
    s_flip180 = on;
    s_prefs.begin("deskbuddy", false);                // read-write
    s_prefs.putBool("flip180", on);
    s_prefs.end();
}
bool display_hal_get_flip180(void) { return s_flip180; }

// SH8601 driver doesn't strictly require even alignment in source, but the
// rounder is harmless and keeps behavior consistent with the CO5300 port.
void display_hal_round_area(int32_t* x1, int32_t* y1, int32_t* x2, int32_t* y2) {
    *x1 = *x1 & ~1;
    *y1 = *y1 & ~1;
    *x2 = *x2 | 1;
    *y2 = *y2 | 1;
}
