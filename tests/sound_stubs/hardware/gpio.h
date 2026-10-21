#pragma once
#include <stdbool.h>
#define GPIO_OUT 1
#define GPIO_FUNC_SIO 5
#define GPIO_FUNC_PWM 4
void gpio_init(unsigned pin);
void gpio_put(unsigned pin, bool value);
void gpio_set_dir(unsigned pin, bool out);
void gpio_set_function(unsigned pin, unsigned function);
