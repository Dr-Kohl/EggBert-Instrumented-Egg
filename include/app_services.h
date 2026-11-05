#pragma once

#include <stdbool.h>
#include <stdint.h>

// Firmware-owned services. New live apps normally only need app.h and ui.h.
bool app_host_can_open(void);
void app_host_prepare(void);
void app_host_home(void);

typedef enum { CAPTURE_APP_RECORD, CAPTURE_APP_CATCH, CAPTURE_APP_BEAM, CAPTURE_APP_HAMMER } CaptureApp;
void capture_app_enter(CaptureApp app);
void capture_app_draw(void);
void capture_app_button(unsigned button);
uint32_t capture_app_sample_period_ms(void);
void capture_app_refresh_live(void);
