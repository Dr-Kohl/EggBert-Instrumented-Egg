#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ssd1306_init(void);
void ssd1306_clear(void);
void ssd1306_text(uint8_t row, const char *text);
void ssd1306_show(void);
