#include "../../hal/audio_hal.h"

// AMOLED-2.16 (upstream Clawdmeter board) — no desk-buddy audio cues.
// No-op so shared code can call the audio HAL unconditionally.
void audio_hal_init(void) {}
void audio_hal_cue(audio_cue_t cue) { (void)cue; }
