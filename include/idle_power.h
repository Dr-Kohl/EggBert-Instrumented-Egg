#pragma once
#include <stdbool.h>
#include <stdint.h>

// One-minute bench setting. Change to 15u * 60u * 1000u after validation.
#ifndef IDLE_TIMEOUT_MS
#define IDLE_TIMEOUT_MS 60000u
#endif

typedef struct {
    uint32_t last_activity_ms;
    int16_t anchor[3];
    bool have_anchor;
} idle_power_t;

void idle_power_init(idle_power_t *idle, uint32_t now_ms);
void idle_power_activity(idle_power_t *idle, uint32_t now_ms);
// Live +/-2 g counts (16384 counts/g). Static gravity and small noise are idle.
void idle_power_motion(idle_power_t *idle, const int16_t xyz[3], uint32_t now_ms);
bool idle_power_due(idle_power_t *idle, uint32_t now_ms, bool inhibited);
