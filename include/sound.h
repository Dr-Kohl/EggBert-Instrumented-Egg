#pragma once

typedef enum {
    SOUND_BUTTON,
    SOUND_READY,
    SOUND_RECORDING,
    SOUND_COMPLETE,
    SOUND_ERROR,
} sound_cue_t;

// Core 0 only. Hardware PWM generates tones; alarm IRQs advance/stop them.
void sound_init(void);
void sound_play(sound_cue_t cue);
void sound_stop(void);
int sound_current_cue(void); // -1 when silent
