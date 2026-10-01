/*
 * RP2354A has 2 MB of in-package flash.  Keep its last two erase sectors
 * (8 KB) out of the application image for redundant calibration records.
 */
FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 2088960
