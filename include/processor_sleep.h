#pragma once
#include <stdint.h>

enum { PROCESSOR_WAKE_BUTTON=1u, PROCESSOR_WAKE_MOTION=2u, PROCESSOR_WAKE_USB=4u };
void processor_sleep_begin(void);
void processor_sleep_end(void);
// Retained-RAM processor sleep until a wake input, USB host, or LED deadline.
// A timer deadline alone returns zero and leaves the display asleep.
unsigned processor_sleep_wait(uint32_t delay_ms);
