#pragma once
#include <stdint.h>
extern uint8_t test_flash[2097152];
#define XIP_BASE ((uintptr_t)test_flash)
