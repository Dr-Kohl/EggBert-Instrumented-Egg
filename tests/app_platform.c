#include <string.h>
#include "app_platform.h"
#include "egg_format.h"
#include "sound.h"
#include "hardware/i2c.h"
#include "hardware/sync.h"

uint32_t test_now;
int test_time_mode=-1;
bool test_sensor_ok=true, test_save_ok=true, test_fifo_ok=true, test_gyro_ok=true;
int16_t test_accel[3]={0,0,16384}, test_gyro[3];
unsigned test_saves, test_gyro_stops, test_fifo_stops, test_frames;
calibration_record_t test_calibration;
uint8_t test_frame[1024];
FILE *test_frame_output;
unsigned test_text_overflows;
lsm6dsv_model_t test_model=LSM6DSV_MODEL_LSM6DSV320X;
static unsigned page;
static bool fifo_dual;
uint8_t test_flash[PICO_FLASH_SIZE_BYTES];
unsigned test_flash_erases, test_flash_programs, test_irq_depth, test_sound_stops;
size_t test_flash_program_limit=FLASH_PAGE_SIZE;
bool test_screen_ok=true, sound_enabled=true;
bool test_usb_connected, test_idle_ok=true, test_wake_ok=true;
unsigned test_idle_starts, test_idle_ends;
uint8_t test_contrast=0x7f;
static bool expect_contrast;
static i2c_inst_t i2c_instance;
i2c_inst_t *i2c0=&i2c_instance;
absolute_time_t get_absolute_time(void) { return test_now; }
uint32_t to_ms_since_boot(absolute_time_t time) { return (uint32_t)time; }
absolute_time_t make_timeout_time_ms(uint32_t ms) { return test_now+ms; }
bool time_reached(absolute_time_t time) { return test_time_mode<0 ? test_now>=time : test_time_mode!=0; }
uint64_t time_us_64(void) { return (uint64_t)test_now*1000; }
void sleep_ms(unsigned ms) { test_now+=ms; }
void gpio_set_function(uint pin,uint function) { (void)pin; (void)function; }
void gpio_pull_up(uint pin) { (void)pin; }
unsigned i2c_init(i2c_inst_t *i2c,unsigned baud) { (void)i2c; return baud; }
int i2c_write_blocking(i2c_inst_t *i2c,uint8_t address,const uint8_t *data,size_t size,bool nostop) {
    (void)i2c; (void)address; (void)nostop;
    if (!test_screen_ok) return -1;
    if (size==2 && data[0]==0) {
        if (expect_contrast) { test_contrast=data[1]; expect_contrast=false; }
        else if (data[1]==0x81) expect_contrast=true;
    }
    if (size==2 && data[0]==0 && (data[1]&0xf8)==0xb0) page=data[1]&7;
    if (size==129 && data[0]==0x40) {
        memcpy(test_frame+page*128,data+1,128);
        if (page==7) {
            ++test_frames;
            if (test_frame_output) fwrite(test_frame,1,sizeof test_frame,test_frame_output);
        }
    }
    return (int)size;
}
void sound_play(sound_cue_t cue) { (void)cue; }
void sound_stop(void) { ++test_sound_stops; }
void sound_set_enabled(bool enabled) { sound_enabled=enabled; if (!enabled) sound_stop(); }
uint32_t save_and_disable_interrupts(void) { return test_irq_depth++; }
void restore_interrupts(uint32_t state) { test_irq_depth=state; }
void flash_range_erase(uint32_t offset,size_t count) {
    ++test_flash_erases;
    memset(test_flash+offset,0xff,count);
}
void flash_range_program(uint32_t offset,const uint8_t *data,size_t count) {
    ++test_flash_programs;
    if (count>test_flash_program_limit) count=test_flash_program_limit;
    for (size_t i=0;i<count;++i) test_flash[offset+i]&=data[i];
}
lsm6dsv_model_t lsm6dsv_model(void) { return test_model; }
bool lsm6dsv_read_accel(int16_t *x,int16_t *y,int16_t *z) {
    *x=test_accel[0]; *y=test_accel[1]; *z=test_accel[2]; return test_sensor_ok;
}
bool lsm6dsv_gyro_start(void) { return test_gyro_ok; }
void lsm6dsv_gyro_stop(void) { ++test_gyro_stops; }
bool lsm6dsv_read_gyro(int16_t *x,int16_t *y,int16_t *z) {
    *x=test_gyro[0]; *y=test_gyro[1]; *z=test_gyro[2]; return test_sensor_ok;
}
bool lsm6dsv_drop_fifo_start(void) { fifo_dual=true; return test_fifo_ok; }
bool lsm6dsv_beam_fifo_start(void) { fifo_dual=false; return test_fifo_ok; }
void lsm6dsv_fifo_stop(void) { fifo_dual=false; ++test_fifo_stops; }
bool lsm6dsv_dual_fifo_active(void) { return fifo_dual; }
uint8_t lsm6dsv_read_ctrl8(void) { return 3; }
const calibration_record_t *calibration_current(void) { return &test_calibration; }
bool calibration_save(const calibration_record_t *candidate) {
    ++test_saves;
    if (!test_save_ok) return false;
    test_calibration=*candidate; ++test_calibration.generation;
    return true;
}
int16_t calibration_correct_axis(int16_t raw,unsigned axis,uint32_t scale) {
    if (!test_calibration.scale_q16[axis]) return raw;
    return egg_correct_low(raw,test_calibration.offset_live_counts[axis],test_calibration.scale_q16[axis],scale);
}

bool stdio_usb_connected(void) { return test_usb_connected; }
bool lsm6dsv_idle_start(void) { ++test_idle_starts; return test_idle_ok; }
bool lsm6dsv_idle_end(void) { ++test_idle_ends; return test_wake_ok; }
