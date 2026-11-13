#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define i2c0 ((void *)1)
#define GPIO_FUNC_I2C 3u
static unsigned transfers, pages, bad, off_commands, on_commands;
static bool reject_power;
void i2c_init(void *bus, unsigned rate) { (void)bus; (void)rate; }
void gpio_set_function(unsigned pin, unsigned function) { (void)pin; (void)function; }
void gpio_pull_up(unsigned pin) { (void)pin; }
void sleep_ms(unsigned ms) { (void)ms; }
int i2c_write_blocking(void *bus, uint8_t address, const uint8_t *data, size_t size, bool nostop) {
    if (bus!=i2c0 || address!=0x3c || nostop) bad=1;
    ++transfers;
    if(size==2 && data[0]==0 && (data[1]==0xae || data[1]==0xaf)) {
        if(reject_power) return -1;
        if(data[1]==0xae) off_commands++; else on_commands++;
    }
    if (size==129 && data[0]==0x40) ++pages;
    else if (size!=2 || data[0]!=0) bad=1;
    return (int)size;
}

// Exercise the actual display module; only the physical I2C/GPIO are stubbed.
#include "../src/ssd1306.c"

int ssd1306_schedule_test(void) {
    ssd1306_service_show();
    if (transfers) return 1;
    if (!ssd1306_init() || bad || pages!=8) return 2;
    pages=transfers=0;
    ssd1306_schedule_show();
    if (transfers) return 3;
    for (unsigned i=0;i<8;++i) {
        ssd1306_service_show();
        if (pages!=i+1 || transfers!=(i+1)*4) return 4;
    }
    ssd1306_service_show();
    if (pages!=8 || transfers!=32) return 5;
    ssd1306_schedule_show(); ssd1306_service_show();
    if (pages!=9) return 6;
    ssd1306_show(); // synchronous idle draw supersedes the pending refresh
    if (pages!=17) return 7;
    ssd1306_service_show();
    if (pages!=17 || bad) return 8;
    ssd1306_schedule_show();
    if(!ssd1306_set_power(false)) return 9;
    unsigned asleep_transfers=transfers;
    ssd1306_service_show(); ssd1306_show(); ssd1306_schedule_show(); ssd1306_service_show();
    if(transfers!=asleep_transfers || off_commands!=2) return 10; // init + standby
    reject_power=true;
    if(ssd1306_set_power(true)) return 11;
    reject_power=false;
    if(!ssd1306_set_power(true) || on_commands!=2) return 12;
    ssd1306_show();
    if(pages!=25 || bad) return 13;
    return 0;
}
