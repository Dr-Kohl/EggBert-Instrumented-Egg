#pragma once
#include <stddef.h>
#include <stdint.h>
#define spi0 0
#define SPI_CPOL_1 1
#define SPI_CPHA_1 1
#define SPI_MSB_FIRST 1
void spi_init(int spi, unsigned baud);
void spi_set_format(int spi, unsigned bits, unsigned cpol, unsigned cpha, unsigned order);
int spi_write_blocking(int spi, const uint8_t *tx, size_t len);
int spi_write_read_blocking(int spi, const uint8_t *tx, uint8_t *rx, size_t len);
