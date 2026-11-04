#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t raw[3];
    int16_t corrected[3];
    float filtered[3];
    uint32_t time_ms;
} AppSample;

// All callbacks run cooperatively on core 0. Missing callbacks are optional.
// draw must only render state; power-header refreshes may also invoke it.
typedef struct App {
    void (*enter)(void);
    void (*leave)(void);
    void (*draw)(void);
    void (*button)(unsigned button); // 0 top, 1 middle, 2 bottom
    void (*tick)(void);
    void (*sample)(const AppSample *sample);
    uint32_t (*sample_period_ms)(void);
    bool owns_imu; // Application services its own live reads (e.g. Maze).
    bool fast_tick;
} App;

typedef struct {
    const char *id;
    const char *label;
    const App *app;
} AppEntry;

// Generated from apps.json at configure time. Order is the Home display order.
extern const AppEntry app_entries[];
extern const size_t app_entry_count;

const App *app_current(void);
const AppSample *app_sample_current(void);
bool app_open(size_t index);
void app_return_home(void);
void app_draw(void);
void app_button(unsigned button);
void app_tick(void);
void app_publish_sample(const AppSample *sample);
uint32_t app_sample_period_ms(void);
