#pragma once
#include <stdbool.h>
typedef unsigned uint;
#define GPIO_FUNC_I2C 3
void gpio_set_function(uint pin, uint function);
void gpio_pull_up(uint pin);
