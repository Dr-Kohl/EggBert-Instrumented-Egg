#include <assert.h>
#include <string.h>
#include "../src/processor_sleep.c"

test_clocks_t test_clocks;
test_scb_t test_scb;
static bool pins[32],usb,masked,pool_full;
static uint32_t events[32],enabled[32],alarm_ms;
static void (*gpio_handler)(void);
static int64_t (*alarm_handler)(alarm_id_t,void*);
static unsigned scenario,wfis,alarms,cancels,fallback;

uint32_t gpio_get_irq_event_mask(uint p) { return events[p]&enabled[p]; }
void gpio_acknowledge_irq(uint p,uint32_t e) { events[p]&=~e; }
void gpio_set_irq_enabled(uint p,uint32_t e,bool on) { if(on)enabled[p]|=e; else enabled[p]&=~e; }
void gpio_add_raw_irq_handler_masked(uint32_t mask,void (*h)(void)) {
    assert(mask==WAKE_MASK && !gpio_handler); gpio_handler=h;
}
void gpio_remove_raw_irq_handler_masked(uint32_t mask,void (*h)(void)) {
    assert(mask==WAKE_MASK && gpio_handler==h); gpio_handler=NULL;
}
bool gpio_get(uint p) { return pins[p]; }
void irq_set_enabled(unsigned irq,bool on) { assert(irq==IO_IRQ_BANK0 && on); }
bool stdio_usb_connected(void) { return usb; }
alarm_id_t add_alarm_in_ms(uint32_t ms,int64_t (*cb)(alarm_id_t,void*),void *d,bool past) {
    (void)d; assert(past); alarm_ms=ms; alarm_handler=cb; alarms++;
    return pool_full ? -1 : 7;
}
bool cancel_alarm(alarm_id_t a) { assert(a==7); cancels++; return true; }
void sleep_ms(unsigned ms) { fallback+=ms; }
uint32_t save_and_disable_interrupts(void) { assert(!masked); masked=true; return 0; }
void restore_interrupts(uint32_t state) {
    assert(state==0 && masked); masked=false;
    assert(test_clocks.sleep_en0==0xffffffffu && test_clocks.sleep_en1==0x7fffffffu);
    assert(test_scb.scr==0x10); // restore before any ISR uses a peripheral
    if(gpio_handler)gpio_handler();
    if(wfis && scenario==0)alarm_handler(7,NULL);
}
void __dsb(void) { assert(masked); }
void __wfi(void) {
    assert(masked && test_scb.scr==0x14);
    assert(!(test_clocks.sleep_en0&CLOCKS_SLEEP_EN0_CLK_SYS_I2C0_BITS));
    assert(!(test_clocks.sleep_en1&CLOCKS_SLEEP_EN1_CLK_SYS_SPI0_BITS));
    // USB, both timers, ticks, SRAM and XIP stay clocked.
    assert((test_clocks.sleep_en1&0x2c1e3ff0u)==0x2c1e3ff0u);
    wfis++;
    if(scenario==1)events[BUTTON_2_PIN]=GPIO_IRQ_EDGE_FALL; // brief released press
    if(scenario==2)events[IMU_INT2_PIN]=GPIO_IRQ_EDGE_RISE;
    if(scenario==3)usb=true;
}
static void fresh(void) {
    processor_sleep_end();
    memset(events,0,sizeof events); memset(enabled,0,sizeof enabled);
    for(unsigned i=0;i<32;i++)pins[i]=true;
    pins[IMU_INT2_PIN]=false;
    usb=masked=pool_full=false; wfis=alarms=cancels=fallback=0;
    test_clocks.sleep_en0=0xffffffffu; test_clocks.sleep_en1=0x7fffffffu; test_scb.scr=0x10;
    processor_sleep_begin(); processor_sleep_begin(); // idempotent, no double handler
}
int processor_sleep_test(void) {
    for(scenario=0;scenario<4;scenario++) {
        fresh(); unsigned reason=processor_sleep_wait(4900);
        assert(reason==(scenario==0?0:scenario==1?PROCESSOR_WAKE_BUTTON:
                        scenario==2?PROCESSOR_WAKE_MOTION:PROCESSOR_WAKE_USB));
        assert(wfis==1 && alarms==1 && cancels==1 && alarm_ms==4900);
    }
    fresh(); pins[IMU_INT2_PIN]=true;
    assert(processor_sleep_wait(100)==PROCESSOR_WAKE_MOTION && wfis==0);
    fresh(); pins[BUTTON_1_PIN]=false;
    assert(processor_sleep_wait(100)==PROCESSOR_WAKE_BUTTON && wfis==0);
    fresh(); pool_full=true;
    assert(processor_sleep_wait(100)==0 && fallback==20 && !wfis && !cancels);
    processor_sleep_end(); processor_sleep_end();
    for(unsigned i=0;i<32;i++)assert(enabled[i]==0);
    assert(!gpio_handler && processor_sleep_wait(100)==0);
    return 0;
}
