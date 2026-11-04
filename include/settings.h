#pragma once

#include <stdbool.h>

typedef enum {
    SETTINGS_LIGHT_LOW,
    SETTINGS_LIGHT_NORMAL,
    SETTINGS_LIGHT_HIGH,
    SETTINGS_LIGHT_COUNT,
} SettingsLight;

typedef struct {
    bool sound;
    SettingsLight light;
} UserSettings;

// Load before the startup sound; apply brightness after OLED initialization.
void settings_init(void);
bool settings_apply_display(void);
const UserSettings *settings_current(void);
void settings_set_sound(bool enabled);
bool settings_set_light(SettingsLight light);
// Only write changed preferences, while acquisition/orientation is idle.
bool settings_save(void);
