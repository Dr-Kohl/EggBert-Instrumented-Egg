#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "hardware/gpio.h"
typedef uint64_t absolute_time_t;
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t time);
absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t time);
uint64_t time_us_64(void);
void sleep_ms(unsigned ms);
