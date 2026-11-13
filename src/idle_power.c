#include "idle_power.h"
#include <string.h>

void idle_power_init(idle_power_t *idle, uint32_t now_ms) {
    memset(idle, 0, sizeof *idle);
    idle->last_activity_ms = now_ms;
}
void idle_power_activity(idle_power_t *idle, uint32_t now_ms) {
    idle->last_activity_ms = now_ms;
}
void idle_power_motion(idle_power_t *idle, const int16_t xyz[3], uint32_t now_ms) {
    bool moved = false;
    for (unsigned axis=0; axis<3; ++axis) {
        int32_t delta = (int32_t)xyz[axis] - idle->anchor[axis];
        if (delta > 1310 || delta < -1310) moved = true; // about 0.08 g
    }
    if (!idle->have_anchor || moved) {
        memcpy(idle->anchor, xyz, sizeof idle->anchor);
        idle->have_anchor = true;
        idle_power_activity(idle, now_ms);
    }
}
bool idle_power_due(idle_power_t *idle, uint32_t now_ms, bool inhibited) {
    // Start a fresh full interval after an experiment/host session finishes.
    if (inhibited) idle_power_activity(idle, now_ms);
    return !inhibited && (uint32_t)(now_ms-idle->last_activity_ms) >= IDLE_TIMEOUT_MS;
}
