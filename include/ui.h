#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ssd1306.h"

bool ui_display_available(void);
void ui_set_display_available(bool available);
void ui_set_power(uint16_t millivolts, bool usb);
void ui_draw_status_header(void);
void ui_begin(const char *title);
void ui_menu_item(uint8_t y, const char *text, bool selected);
