#include <assert.h>
#include <stdio.h>
#include "app_platform.h"
#include "sound.h"
static bool may_save=true;
bool app_host_can_open(void) { return may_save; }
// Expose record fields for corruption/interrupted-write fixtures while testing
// the actual settings implementation, rather than a duplicate storage model.
#include "../src/settings.c"

static void reboot(void) {
    sound_enabled=true; // C startup value on this board.
    settings_init();
}

static void assert_calibration_intact(void) {
    for (size_t i=PICO_FLASH_SIZE_BYTES-2u*FLASH_SECTOR_SIZE;i<PICO_FLASH_SIZE_BYTES;++i)
        assert(test_flash[i]==0xa5);
    assert(test_irq_depth==0);
}

int main(void) {
    memset(test_flash,0xff,sizeof test_flash);
    memset(test_flash+PICO_FLASH_SIZE_BYTES-2u*FLASH_SECTOR_SIZE,0xa5,2u*FLASH_SECTOR_SIZE);
    ui_set_display_available(true);
    reboot();
    assert(settings_current()->sound && settings_current()->light==SETTINGS_LIGHT_NORMAL);
    assert(settings_apply_display() && test_contrast==0x7f);
    assert(settings_save() && test_flash_programs==0);
    assert(!settings_set_light((SettingsLight)-1));
    assert(!settings_set_light(SETTINGS_LIGHT_COUNT));
    test_screen_ok=false;
    assert(!settings_set_light(SETTINGS_LIGHT_HIGH));
    assert(settings_current()->light==SETTINGS_LIGHT_NORMAL);
    test_screen_ok=true;
    settings_set_sound(false); assert(!sound_enabled);
    assert(settings_set_light(SETTINGS_LIGHT_LOW) && test_contrast==0x10);
    may_save=false; assert(!settings_save() && test_flash_programs==0);
    may_save=true; assert(settings_save() && test_flash_programs==1);
    assert(saved_offset==SETTINGS_FLASH_A && saved_generation==1);
    assert_calibration_intact();
    assert(settings_save() && test_flash_programs==1); // No-change exit.
    reboot(); assert(!sound_enabled && settings_current()->light==SETTINGS_LIGHT_LOW);
    settings_set_sound(true); assert(settings_set_light(SETTINGS_LIGHT_HIGH));
    assert(settings_save() && saved_offset==SETTINGS_FLASH_B && saved_generation==2);
    test_flash[SETTINGS_FLASH_B+offsetof(SettingsRecord,crc)]^=1;
    reboot(); // Corrupt newest record: recover the preceding complete record.
    assert(!sound_enabled && settings_current()->light==SETTINGS_LIGHT_LOW);
    settings_set_sound(true);
    test_flash_program_limit=8; assert(!settings_save());
    assert(saved_offset==SETTINGS_FLASH_A && saved_generation==1);
    reboot(); assert(!sound_enabled && settings_current()->light==SETTINGS_LIGHT_LOW);
    settings_set_sound(true); test_flash_program_limit=FLASH_PAGE_SIZE;
    assert(settings_save()); reboot(); assert(sound_enabled);
    assert_calibration_intact();
    // Generation wrap still chooses the most recent valid record.
    SettingsRecord last=read_record(SETTINGS_FLASH_B);
    last.generation=UINT32_MAX;
    last.crc=egg_crc32((const uint8_t*)&last,offsetof(SettingsRecord,crc));
    memset(test_flash+SETTINGS_FLASH_A,0xff,2u*FLASH_SECTOR_SIZE);
    memcpy(test_flash+SETTINGS_FLASH_B,&last,sizeof last);
    reboot(); assert(saved_generation==UINT32_MAX);
    settings_set_sound(false); assert(settings_save() && saved_generation==0);
    reboot(); assert(!sound_enabled && saved_generation==0);
    assert_calibration_intact();
    // CRC-valid unsupported/corrupt values are rejected as well.
    SettingsRecord invalid=read_record(SETTINGS_FLASH_A);
    invalid.light=SETTINGS_LIGHT_COUNT;
    invalid.crc=egg_crc32((const uint8_t*)&invalid,offsetof(SettingsRecord,crc));
    memcpy(test_flash+SETTINGS_FLASH_A,&invalid,sizeof invalid);
    reboot(); assert(sound_enabled && saved_generation==UINT32_MAX);
    memset(test_flash+SETTINGS_FLASH_A,0,2u*FLASH_SECTOR_SIZE);
    reboot(); assert(sound_enabled && settings_current()->light==SETTINGS_LIGHT_NORMAL);
    assert_calibration_intact();
    puts("Settings: preview, save/reboot, idle guard, CRC recovery, interrupted writes and calibration isolation passed.");
    return 0;
}
