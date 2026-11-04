#pragma once
#include <stdio.h>
#include "app.h"
#include "app_services.h"
#include "calibration.h"
#include "lsm6dsv.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
extern uint32_t test_now;
extern int test_time_mode; // -1 real simulated time, 0 never ready, 1 ready
extern bool test_sensor_ok, test_save_ok, test_fifo_ok, test_gyro_ok;
extern int16_t test_accel[3], test_gyro[3];
extern unsigned test_saves, test_gyro_stops, test_fifo_stops, test_frames;
extern calibration_record_t test_calibration;
extern uint8_t test_frame[1024];
extern FILE *test_frame_output;
extern unsigned test_text_overflows;
extern lsm6dsv_model_t test_model;
extern uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
extern unsigned test_flash_erases, test_flash_programs, test_irq_depth, test_sound_stops;
extern size_t test_flash_program_limit;
extern bool test_screen_ok;
extern uint8_t test_contrast;
