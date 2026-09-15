#pragma once

#include <stdbool.h>
#include <stdint.h>

bool ssd1306_init(void);
void ssd1306_clear(void);
void ssd1306_text(uint8_t row, const char *text);
// Logical portrait canvas: 64 pixels wide by 128 pixels tall.  Coordinates
// are rotated 90 degrees counterclockwise into the physical 128x64 panel.
void ssd1306_ui_pixel(uint8_t x, uint8_t y, bool on);
void ssd1306_ui_fill_rect(uint8_t x, uint8_t y, uint8_t width, uint8_t height, bool on);
void ssd1306_ui_text(uint8_t x, uint8_t y, const char *text, bool on);
void ssd1306_ui_text_scaled(uint8_t x, uint8_t y, const char *text, bool on, uint8_t scale);
void ssd1306_show(void);
