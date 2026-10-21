#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef unsigned int uint;
#define GPIO_OUT 1
#define GPIO_IN 0
#define GPIO_FUNC_SPI 1
#define GPIO_IRQ_EDGE_RISE 8
void gpio_put(uint pin, bool value);
void gpio_init(uint pin);
void gpio_set_dir(uint pin, bool out);
void gpio_set_function(uint pin, uint function);
void gpio_set_irq_enabled(uint pin, uint32_t events, bool enabled);
void gpio_set_irq_enabled_with_callback(uint pin, uint32_t events, bool enabled,
                                        void (*callback)(uint, uint32_t));
