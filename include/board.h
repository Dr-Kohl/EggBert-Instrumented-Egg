#pragma once

#include "hardware/i2c.h"

// Extracted from RP2354_GPIO_Netlist_Egg_Projects.md.
#define LED_GREEN_PIN       1u
#define LED_YELLOW_PIN      2u
#define LED_RED_PIN         12u

#define BUTTON_1_PIN        0u   // SW5, pressed = GND
#define BUTTON_2_PIN        3u   // SW2, pressed = GND
#define BUTTON_3_PIN        13u  // SW3, pressed = GND

#define OLED_I2C            i2c0
#define OLED_SDA_PIN        24u  // DISPLAY pin 4, 4.7k pull-up fitted
#define OLED_SCL_PIN        25u  // DISPLAY pin 3, 4.7k pull-up fitted
#define OLED_I2C_BAUD       400000u

// Most 0.96 inch SSD1306 modules are 0x3C. Change with -DOLED_I2C_ADDRESS=0x3D.
#ifndef OLED_I2C_ADDRESS
#define OLED_I2C_ADDRESS    0x3Cu
#endif
