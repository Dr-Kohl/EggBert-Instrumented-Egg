#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { float divider; uint16_t wrap; } pwm_config;
unsigned pwm_gpio_to_slice_num(unsigned pin);
void pwm_set_enabled(unsigned slice, bool enabled);
pwm_config pwm_get_default_config(void);
void pwm_config_set_clkdiv(pwm_config *config, float divider);
void pwm_config_set_wrap(pwm_config *config, uint16_t wrap);
void pwm_init(unsigned slice, pwm_config *config, bool enabled);
void pwm_set_gpio_level(unsigned pin, uint16_t level);
