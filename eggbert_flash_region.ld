/*
 * RP2354A has 2 MB of in-package flash. Reserve four erase sectors (16 KB):
 * two for user settings, then the two existing calibration sectors. Their
 * original calibration offsets are preserved for firmware upgrades.
 */
FLASH(rx) : ORIGIN = 0x10000000, LENGTH = 2080768
