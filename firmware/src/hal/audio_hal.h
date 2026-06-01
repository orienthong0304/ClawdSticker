#pragma once

// Board-agnostic audio cue interface. Boards without a codec provide a no-op
// implementation so shared code (main.cpp) can call these unconditionally.

enum audio_cue_t {
    AUDIO_CUE_WAITING,   // attention — two rising beeps
    AUDIO_CUE_DONE,      // success   — three rising beeps
    AUDIO_CUE_ERROR,     // failure   — two descending beeps
};

// Bring up the codec + I2S and start the audio task. Safe to call once in
// setup(). On failure it logs and leaves cues as no-ops.
void audio_hal_init(void);

// Queue a short cue tone. Non-blocking — playback happens on the audio task,
// so it never stalls the LVGL render loop.
void audio_hal_cue(audio_cue_t cue);
