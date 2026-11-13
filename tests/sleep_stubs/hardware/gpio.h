#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef unsigned uint;
#define GPIO_IRQ_EDGE_FALL 4u
#define GPIO_IRQ_EDGE_RISE 8u
uint32_t gpio_get_irq_event_mask(uint pin);
void gpio_acknowledge_irq(uint pin,uint32_t events);
void gpio_set_irq_enabled(uint pin,uint32_t events,bool enable);
void gpio_add_raw_irq_handler_masked(uint32_t mask,void (*handler)(void));
void gpio_remove_raw_irq_handler_masked(uint32_t mask,void (*handler)(void));
bool gpio_get(uint pin);
