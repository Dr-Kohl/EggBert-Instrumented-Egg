#include "processor_sleep.h"
#include "board.h"
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/platform/cpu_regs.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/structs/clocks.h"
#include "hardware/structs/scb.h"
#include "hardware/regs/clocks.h"

// Do not reset the chip, stop the timebase, or disconnect USB. SLEEPDEEP gates
// clocks while WFI is active; RAM, timer alarms and USB remain available. PLLs
// retain their settings, so SPI/I2C/USB rates need no reinitialization on wake.
static const uint wake_pins[]={BUTTON_1_PIN,BUTTON_2_PIN,BUTTON_3_PIN,IMU_INT2_PIN};
#define WAKE_MASK ((1u<<BUTTON_1_PIN)|(1u<<BUTTON_2_PIN)|(1u<<BUTTON_3_PIN)|(1u<<IMU_INT2_PIN))
static volatile unsigned pending;
static volatile bool deadline;
static bool armed;

static void wake_irq(void) {
    for (unsigned i=0;i<4;i++) {
        uint32_t events=gpio_get_irq_event_mask(wake_pins[i]);
        if (!events) continue;
        gpio_acknowledge_irq(wake_pins[i],events);
        pending |= i==3 ? PROCESSOR_WAKE_MOTION : PROCESSOR_WAKE_BUTTON;
    }
}
void processor_sleep_begin(void) {
    if (armed) return;
    pending=0;
    gpio_add_raw_irq_handler_masked(WAKE_MASK,wake_irq);
    for (unsigned i=0;i<4;i++) {
        uint32_t edge=i==3 ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL;
        gpio_acknowledge_irq(wake_pins[i],edge);
        gpio_set_irq_enabled(wake_pins[i],edge,true);
    }
    irq_set_enabled(IO_IRQ_BANK0,true);
    armed=true;
}
void processor_sleep_end(void) {
    if (!armed) return;
    for (unsigned i=0;i<4;i++) {
        uint32_t edge=i==3 ? GPIO_IRQ_EDGE_RISE : GPIO_IRQ_EDGE_FALL;
        gpio_set_irq_enabled(wake_pins[i],edge,false);
        gpio_acknowledge_irq(wake_pins[i],edge);
    }
    gpio_remove_raw_irq_handler_masked(WAKE_MASK,wake_irq);
    pending=0; armed=false;
}
static int64_t timer_wake(alarm_id_t id, void *data) {
    (void)id; (void)data; deadline=true; return 0;
}
unsigned processor_sleep_wait(uint32_t delay_ms) {
    if (!armed) return 0;
    deadline=false;
    alarm_id_t alarm=add_alarm_in_ms(delay_ms,timer_wake,NULL,true);
    if (alarm<0) { sleep_ms(20); return 0; } // safe fallback if alarm pool is full
    unsigned reason=0;
    while (!deadline && !reason) {
        // Level checks close the race between configuring the sensor and GPIO
        // IRQs. Latched motion also survives an event before entry into WFI.
        if (!gpio_get(BUTTON_1_PIN)||!gpio_get(BUTTON_2_PIN)||!gpio_get(BUTTON_3_PIN))
            reason |= PROCESSOR_WAKE_BUTTON;
        if (gpio_get(IMU_INT2_PIN)) reason |= PROCESSOR_WAKE_MOTION;
        if (stdio_usb_connected()) reason |= PROCESSOR_WAKE_USB;
        uint32_t irq_state=save_and_disable_interrupts();
        reason |= pending; pending=0;
        if (!reason && !deadline) {
            uint32_t en0=clocks_hw->sleep_en0, en1=clocks_hw->sleep_en1;
            uint32_t scr=scb_hw->scr;
            // Keep SRAM, flash, GPIO, both timers/ticks and USB clocks. Gate
            // unused acquisition/display/sound engines only while asleep.
            clocks_hw->sleep_en0=en0 & ~(CLOCKS_SLEEP_EN0_CLK_SYS_ADC_BITS |
                CLOCKS_SLEEP_EN0_CLK_ADC_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_I2C0_BITS |
                CLOCKS_SLEEP_EN0_CLK_SYS_I2C1_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PWM_BITS |
                CLOCKS_SLEEP_EN0_CLK_SYS_PIO0_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_PIO1_BITS |
                CLOCKS_SLEEP_EN0_CLK_SYS_PIO2_BITS | CLOCKS_SLEEP_EN0_CLK_SYS_SHA256_BITS);
            clocks_hw->sleep_en1=en1 & ~(CLOCKS_SLEEP_EN1_CLK_SYS_SPI0_BITS |
                CLOCKS_SLEEP_EN1_CLK_PERI_SPI0_BITS | CLOCKS_SLEEP_EN1_CLK_SYS_SPI1_BITS |
                CLOCKS_SLEEP_EN1_CLK_PERI_SPI1_BITS);
            scb_hw->scr=scr | ARM_CPU_PREFIXED(SCR_SLEEPDEEP_BITS);
            __dsb();
            // PRIMASK prevents a handler consuming the wake event before WFI.
            // A pending enabled IRQ still releases WFI with PRIMASK set.
            __wfi();
            scb_hw->scr=scr;
            clocks_hw->sleep_en0=en0; clocks_hw->sleep_en1=en1;
        }
        restore_interrupts(irq_state);
    }
    cancel_alarm(alarm);
    return reason;
}
