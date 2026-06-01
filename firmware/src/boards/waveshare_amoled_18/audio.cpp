#include "../../hal/audio_hal.h"
#include "board.h"
#include "io_expander.h"
#include "es8311.h"
#include <Arduino.h>
#include <Wire.h>
#include <ESP_I2S.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Cue tones for the desk-buddy. Simple program-generated sine beeps (no audio
// files) per docs/UI_SPEC. Playback runs on a dedicated FreeRTOS task so the
// blocking I2S writes never stall the LVGL loop.

#define SAMPLE_RATE 16000

static I2SClass        i2s;
static es8311_handle_t es = nullptr;
static QueueHandle_t   cue_q = nullptr;
static bool            audio_ok = false;

struct ToneStep { uint16_t freq; uint16_t ms; };  // freq 0 = silent gap

// Cue patterns (freq Hz, duration ms).
static const ToneStep CUE_WAITING[] = {{880, 120}, {0, 45}, {1175, 150}};
static const ToneStep CUE_DONE[]    = {{660, 90}, {784, 90}, {1047, 170}};
static const ToneStep CUE_ERROR[]   = {{440, 150}, {0, 35}, {294, 220}};
// Urgent alarm — high/low/high triad with snappy gaps.
static const ToneStep CUE_DANGER[]  = {{1320, 90}, {0, 30}, {988, 90}, {0, 30}, {1320, 130}};

static void play_tone(uint16_t freq, uint16_t ms) {
    const int n = (int)SAMPLE_RATE * ms / 1000;
    const int fade = SAMPLE_RATE * 5 / 1000;     // 5 ms in/out fade kills clicks
    static int16_t buf[256 * 2];                 // stereo frames
    const double dp = 2.0 * M_PI * freq / SAMPLE_RATE;
    double phase = 0.0;
    int done = 0;
    while (done < n) {
        int chunk = n - done; if (chunk > 256) chunk = 256;
        for (int i = 0; i < chunk; i++) {
            int idx = done + i;
            double amp = 0.45;                   // headroom, avoid harsh clipping
            if (idx < fade)            amp *= (double)idx / fade;
            else if (idx > n - fade)   amp *= (double)(n - idx) / fade;
            int16_t s = (int16_t)(sin(phase) * amp * 32767.0);
            phase += dp; if (phase > 2 * M_PI) phase -= 2 * M_PI;
            buf[i * 2] = s; buf[i * 2 + 1] = s;
        }
        i2s.write((uint8_t*)buf, chunk * 2 * sizeof(int16_t));
        done += chunk;
    }
}

static void play_seq(const ToneStep* seq, int len) {
    // Enable the speaker amp (drive both candidates — see board.h note).
    digitalWrite(AUDIO_PA_EN_GPIO, HIGH);
    io_expander_set(IOX_PIN_PA_EN, true);
    delay(8);  // amp settle
    for (int i = 0; i < len; i++) play_tone(seq[i].freq, seq[i].ms);
    delay(12);
    // Amp off between cues to avoid idle hiss / save power.
    digitalWrite(AUDIO_PA_EN_GPIO, LOW);
    io_expander_set(IOX_PIN_PA_EN, false);
}

static void audio_task(void* arg) {
    (void)arg;
    audio_cue_t cue;
    for (;;) {
        if (xQueueReceive(cue_q, &cue, portMAX_DELAY) == pdTRUE) {
            switch (cue) {
            case AUDIO_CUE_WAITING: play_seq(CUE_WAITING, 3); break;
            case AUDIO_CUE_DONE:    play_seq(CUE_DONE, 3);    break;
            case AUDIO_CUE_ERROR:   play_seq(CUE_ERROR, 3);   break;
            case AUDIO_CUE_DANGER:  play_seq(CUE_DANGER, 5);  break;
            }
        }
    }
}

void audio_hal_init(void) {
    pinMode(AUDIO_PA_EN_GPIO, OUTPUT);
    digitalWrite(AUDIO_PA_EN_GPIO, LOW);  // amp off until a cue plays

    i2s.setPins(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("AUDIO: I2S begin failed");
        return;
    }

    // ES8311 shares the board I2C bus (port 0, brought up by board_init's Wire).
    es = es8311_create(0, ES8311_ADDR);
    if (!es) { Serial.println("AUDIO: es8311_create failed"); return; }

    es8311_clock_config_t clk = {};
    clk.mclk_inverted     = false;
    clk.sclk_inverted     = false;
    clk.mclk_from_mclk_pin = true;
    clk.mclk_frequency    = SAMPLE_RATE * 256;
    clk.sample_frequency  = SAMPLE_RATE;
    if (es8311_init(es, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("AUDIO: es8311_init failed");
        return;
    }
    es8311_sample_frequency_config(es, clk.mclk_frequency, clk.sample_frequency);
    es8311_microphone_config(es, false);
    es8311_voice_volume_set(es, 85, NULL);

    cue_q = xQueueCreate(4, sizeof(audio_cue_t));
    xTaskCreatePinnedToCore(audio_task, "audio", 4096, NULL, 1, NULL, 1);

    audio_ok = true;
    Serial.println("AUDIO: ES8311 ready");
}

void audio_hal_cue(audio_cue_t cue) {
    if (audio_ok && cue_q) xQueueSend(cue_q, &cue, 0);
}
