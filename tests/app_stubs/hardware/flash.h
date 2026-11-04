#pragma once
#include <stddef.h>
#include <stdint.h>
#define PICO_FLASH_SIZE_BYTES 2097152u
#define FLASH_SECTOR_SIZE 4096u
#define FLASH_PAGE_SIZE 256u
void flash_range_erase(uint32_t offset,size_t count);
void flash_range_program(uint32_t offset,const uint8_t *data,size_t count);
