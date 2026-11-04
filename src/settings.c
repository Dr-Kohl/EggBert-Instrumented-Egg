#include "settings.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "app_services.h"
#include "egg_format.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "sound.h"
#include "ui.h"

// Settings occupy the two sectors immediately BEFORE the existing calibration
// sectors. Calibration stays at its original offsets for firmware upgrades.
#define SETTINGS_FLASH_A (PICO_FLASH_SIZE_BYTES - 4u*FLASH_SECTOR_SIZE)
#define SETTINGS_FLASH_B (PICO_FLASH_SIZE_BYTES - 3u*FLASH_SECTOR_SIZE)
#define SETTINGS_MAGIC 0x53455431u /* SET1 */
#define SETTINGS_VERSION 1u

typedef struct {
    uint32_t magic, version, generation;
    uint8_t sound, light, reserved[2];
    uint32_t crc;
} SettingsRecord;
_Static_assert(sizeof(SettingsRecord)==20, "Settings record layout changed");
_Static_assert(offsetof(SettingsRecord,crc)==16, "Settings CRC offset changed");
_Static_assert(sizeof(SettingsRecord)<=FLASH_PAGE_SIZE, "Settings must fit a flash page");

static UserSettings current, saved;
static bool have_saved;
static uint32_t saved_generation, saved_offset;
static const uint8_t contrast[SETTINGS_LIGHT_COUNT]={0x10,0x7f,0xff};

static SettingsRecord read_record(uint32_t offset) {
    SettingsRecord record;
    memcpy(&record,(const void *)(XIP_BASE+offset),sizeof record);
    return record;
}

static bool valid_record(const SettingsRecord *record) {
    return record->magic==SETTINGS_MAGIC && record->version==SETTINGS_VERSION &&
        record->sound<=1 && record->light<SETTINGS_LIGHT_COUNT &&
        !record->reserved[0] && !record->reserved[1] &&
        record->crc==egg_crc32((const uint8_t *)record,offsetof(SettingsRecord,crc));
}

static bool newer_generation(uint32_t a,uint32_t b) {
    uint32_t difference=a-b;
    return difference && difference<0x80000000u;
}

void settings_init(void) {
    SettingsRecord a=read_record(SETTINGS_FLASH_A), b=read_record(SETTINGS_FLASH_B);
    bool a_ok=valid_record(&a), b_ok=valid_record(&b);
    have_saved=a_ok || b_ok;
    current=(UserSettings){.sound=sound_enabled,.light=SETTINGS_LIGHT_NORMAL};
    saved_generation=0;
    saved_offset=SETTINGS_FLASH_B; // First write goes to A.
    if (have_saved) {
        bool use_b=b_ok && (!a_ok || newer_generation(b.generation,a.generation));
        const SettingsRecord *record=use_b ? &b : &a;
        current=(UserSettings){.sound=record->sound!=0,.light=(SettingsLight)record->light};
        saved_generation=record->generation;
        saved_offset=use_b ? SETTINGS_FLASH_B : SETTINGS_FLASH_A;
    }
    saved=current;
    sound_set_enabled(current.sound);
}

bool settings_apply_display(void) {
    return !ui_display_available() || ssd1306_set_contrast(contrast[current.light]);
}

const UserSettings *settings_current(void) { return &current; }

void settings_set_sound(bool enabled) {
    current.sound=enabled;
    sound_set_enabled(enabled);
}

bool settings_set_light(SettingsLight light) {
    if ((unsigned)light>=SETTINGS_LIGHT_COUNT) return false;
    if (ui_display_available() && !ssd1306_set_contrast(contrast[light])) return false;
    current.light=light;
    return true;
}

bool settings_save(void) {
    if (!app_host_can_open()) return false;
    // Even an unsaved factory/default configuration needs no write unless the
    // user changes it. Rapidly cycling a row also does not wear flash.
    if (current.sound==saved.sound && current.light==saved.light) return true;
    SettingsRecord record={.magic=SETTINGS_MAGIC,.version=SETTINGS_VERSION,
        .generation=have_saved ? saved_generation+1u : 1u,
        .sound=current.sound,.light=(uint8_t)current.light};
    record.crc=egg_crc32((const uint8_t *)&record,offsetof(SettingsRecord,crc));
    uint32_t destination=saved_offset==SETTINGS_FLASH_A ? SETTINGS_FLASH_B : SETTINGS_FLASH_A;
    _Alignas(4) uint8_t page[FLASH_PAGE_SIZE];
    memset(page,0xff,sizeof page);
    memcpy(page,&record,sizeof record);
    // Stop alarm-driven tones before interrupts are suspended for flash I/O.
    sound_stop();
    uint32_t interrupts=save_and_disable_interrupts();
    flash_range_erase(destination,FLASH_SECTOR_SIZE);
    flash_range_program(destination,page,sizeof page);
    restore_interrupts(interrupts);
    SettingsRecord verified=read_record(destination);
    if (!valid_record(&verified) || memcmp(&verified,&record,sizeof record)) return false;
    have_saved=true;
    saved=current;
    saved_generation=record.generation;
    saved_offset=destination;
    return true;
}
