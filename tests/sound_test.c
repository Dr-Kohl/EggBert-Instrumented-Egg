#include "sound.h"
#include "board.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

static int failed, cancelled, adds, next_id;
static bool enabled, pin_high, alarm_failure;
static unsigned function, level, duration, depth;
static pwm_config applied;
static alarm_callback_t callback;
static alarm_id_t id;
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
#define PIN(p) do { if ((p)!=22u) failed=1; } while (0)
void gpio_init(unsigned p) { PIN(p); }
void gpio_put(unsigned p, bool value) { PIN(p); pin_high=value; }
void gpio_set_dir(unsigned p, bool out) { PIN(p); if (!out) failed=1; }
void gpio_set_function(unsigned p, unsigned f) { PIN(p); function=f; }
unsigned pwm_gpio_to_slice_num(unsigned p) { PIN(p); return 3; }
void pwm_set_enabled(unsigned s, bool e) { if (s!=3) failed=1; enabled=e; }
pwm_config pwm_get_default_config(void) { return (pwm_config){1,65535}; }
void pwm_config_set_clkdiv(pwm_config *c, float d) { c->divider=d; }
void pwm_config_set_wrap(pwm_config *c, uint16_t w) { c->wrap=w; }
void pwm_init(unsigned s, pwm_config *c, bool e) { if (s!=3 || e) failed=1; applied=*c; }
void pwm_set_gpio_level(unsigned p, uint16_t l) { PIN(p); level=l; }
uint32_t clock_get_hz(unsigned c) { (void)c; return 150000000; }
uint32_t save_and_disable_interrupts(void) { return depth++; }
void restore_interrupts(uint32_t state) { depth=state; }
alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t cb, void *data, bool fire) {
    (void)data; (void)fire; ++adds; duration=ms; callback=cb;
    if (alarm_failure) return -1;
    id=++next_id; return id;
}
bool cancel_alarm(alarm_id_t which) { if (which!=id) failed=1; ++cancelled; return true; }

int sound_test(void) {
    sound_play(SOUND_BUTTON); CHECK(adds==0);
    sound_init(); CHECK(!enabled && !pin_high && function==GPIO_FUNC_SIO);
    sound_play(SOUND_BUTTON);
    CHECK(sound_current_cue()==SOUND_BUTTON && enabled && duration==12);
    CHECK(applied.divider==150 && applied.wrap==332 && level==166);
    CHECK(callback(id,NULL)==0 && sound_current_cue()==-1 && !enabled && !pin_high);
    sound_play(SOUND_COMPLETE);
    CHECK(duration==55 && enabled);
    sound_play(SOUND_BUTTON); CHECK(sound_current_cue()==SOUND_COMPLETE && adds==2);
    CHECK(callback(id,NULL)==35000 && !enabled && function==GPIO_FUNC_SIO);
    CHECK(callback(id,NULL)==85000 && enabled && applied.wrap==332);
    CHECK(callback(id,NULL)==0 && !enabled && sound_current_cue()==-1);
    sound_play(SOUND_READY); alarm_id_t stale=id;
    sound_play(SOUND_ERROR); CHECK(cancelled==1 && duration==90);
    CHECK(callback(stale,NULL)==0 && sound_current_cue()==SOUND_ERROR);
    sound_stop(); CHECK(cancelled==2 && !enabled && !pin_high && function==GPIO_FUNC_SIO);
    alarm_failure=true; sound_play(SOUND_RECORDING);
    CHECK(sound_current_cue()==-1 && !enabled && !pin_high);
    alarm_failure=false;
    for (unsigned cue=0; cue<=SOUND_ERROR; ++cue) {
        sound_play(cue);
        unsigned notes=0;
        while (callback(id,NULL)) { CHECK(++notes<6); }
        CHECK(!enabled && !pin_high && sound_current_cue()==-1);
    }
    sound_play(SOUND_STARTUP);
    CHECK(duration==120 && applied.wrap==477);
    CHECK(callback(id,NULL)==120000 && applied.wrap==378);
    CHECK(callback(id,NULL)==180000 && applied.wrap==318);
    CHECK(callback(id,NULL)==120000 && applied.wrap==378);
    CHECK(callback(id,NULL)==360000 && applied.wrap==238);
    CHECK(callback(id,NULL)==0 && !enabled && function==GPIO_FUNC_SIO);
    int old_adds=adds; sound_play((sound_cue_t)99); CHECK(adds==old_adds);
    CHECK(depth==0 && !failed);
    return 0;
}
