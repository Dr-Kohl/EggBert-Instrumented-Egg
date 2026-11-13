#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int32_t alarm_id_t;
alarm_id_t add_alarm_in_ms(uint32_t ms,int64_t (*cb)(alarm_id_t,void*),void *data,bool past);
bool cancel_alarm(alarm_id_t id);
void sleep_ms(unsigned ms);
