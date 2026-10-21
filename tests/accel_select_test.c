#include "accel_select.h"
int accel_select_test(void) {
    if (accel_select_high(false,24575,24575,1200)) return 1;
    if (!accel_select_high(false,24576,24576,1200)) return 2;
    if (!accel_select_high(false,20000,24576,1200)) return 3;
    if (!accel_select_high(true,14000,14000,800)) return 4;
    if (accel_select_high(true,14000,14000,767)) return 5;
    if (!accel_select_high(true,16384,14000,600)) return 6;
    if (!accel_select_high(true,14000,32767,600)) return 7;
    if (accel_high_peak_low_counts(9600) != 204807) return 8;
    if (accel_high_peak_low_counts(32768) < 690000) return 9;
    return 0;
}
