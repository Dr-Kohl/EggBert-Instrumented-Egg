#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/gpio.h"
typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t, void *);
alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t callback, void *data, bool fire_if_past);
bool cancel_alarm(alarm_id_t id);
