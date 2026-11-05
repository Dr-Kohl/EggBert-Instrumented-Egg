#pragma once
#include <assert.h>
#include "app_platform.h"
// Include production app translation units to expose their private states for
// deterministic fixtures. Firmware still compiles each app separately.
#include "../src/apps/record.c"
#include "../src/apps/catch.c"
#include "../src/apps/pendulum.c"
#include "../src/apps/acceleration.c"
#include "../src/apps/gyro.c"
#include "../src/apps/level.c"
#include "../src/apps/friction.c"
#include "../src/apps/beam.c"
#include "../src/apps/hammer.c"
#include "../src/apps/maze.c"
#include "../src/apps/calibrate.c"
#include "../src/apps/raw.c"
#include "../src/apps/settings.c"
#include "../build/app-core-test.inc"

static size_t test_app_index(const char *id) {
    for (size_t i=0;i<app_entry_count;++i) if (!strcmp(app_entries[i].id,id)) return i;
    return app_entry_count;
}
