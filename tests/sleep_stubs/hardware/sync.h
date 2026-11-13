#pragma once
#include <stdint.h>
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t state);
void __dsb(void);
void __wfi(void);
