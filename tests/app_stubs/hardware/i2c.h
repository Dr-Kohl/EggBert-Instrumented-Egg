#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { unsigned unused; } i2c_inst_t;
extern i2c_inst_t *i2c0;
unsigned i2c_init(i2c_inst_t *i2c, unsigned baud);
int i2c_write_blocking(i2c_inst_t *i2c, uint8_t address, const uint8_t *data, size_t size, bool nostop);
