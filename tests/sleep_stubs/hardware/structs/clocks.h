#pragma once
#include <stdint.h>
typedef struct {volatile uint32_t sleep_en0,sleep_en1;} test_clocks_t;
extern test_clocks_t test_clocks;
#define clocks_hw (&test_clocks)
