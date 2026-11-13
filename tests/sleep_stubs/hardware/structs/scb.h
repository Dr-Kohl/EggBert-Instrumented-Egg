#pragma once
#include <stdint.h>
typedef struct {volatile uint32_t scr;} test_scb_t;
extern test_scb_t test_scb;
#define scb_hw (&test_scb)
