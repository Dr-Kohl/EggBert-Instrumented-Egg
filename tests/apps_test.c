#include <assert.h>
#include "app_fixture.h"

static void open_app(const char *id) {
    capture_active=false;
    assert(app_open(test_app_index(id)));
    assert(app_current()==app_entries[test_app_index(id)].app);
}

static void test_settings_controls(void) {
    memset(test_flash,0xff,sizeof test_flash);
    memset(test_flash+PICO_FLASH_SIZE_BYTES-2u*FLASH_SECTOR_SIZE,0xa5,2u*FLASH_SECTOR_SIZE);
    sound_enabled=true;
    settings_init();
    capture_complete=true; capture_count=7200;
    capture_metadata.calibration_generation=29;
    memset(capture_samples,0x35,sizeof capture_samples);
    open_app("settings");
    assert(settings_selection==0 && settings_current()->sound);
    unsigned stopped=test_sound_stops, writes=test_flash_programs;
    handle_button_press(1); // Sound off, including an already active tone.
    assert(!sound_enabled && !settings_current()->sound && test_sound_stops>stopped);
    handle_button_press(2); handle_button_press(1); // Normal -> high.
    assert(settings_current()->light==SETTINGS_LIGHT_HIGH && test_contrast==0xff);
    handle_button_press(1); // High -> low.
    assert(settings_current()->light==SETTINGS_LIGHT_LOW && test_contrast==0x10);
    assert(test_flash_programs==writes); // Previewing does not write flash.
    handle_button_press(2); // SAVE EXIT.
    test_flash_program_limit=8; handle_button_press(1);
    assert(app_current()==&settings_app && !strcmp(settings_feedback,"SAVE ERROR"));
    test_flash_program_limit=FLASH_PAGE_SIZE; handle_button_press(1);
    assert(!app_current() && home_menu.selected==test_app_index("settings"));
    assert(capture_complete && capture_count==7200 && capture_metadata.calibration_generation==29);
    for(size_t i=0;i<sizeof capture_samples;++i) assert(((uint8_t*)capture_samples)[i]==0x35);
    for(size_t i=PICO_FLASH_SIZE_BYTES-2u*FLASH_SECTOR_SIZE;i<PICO_FLASH_SIZE_BYTES;++i)
        assert(test_flash[i]==0xa5); // Existing calibration sectors untouched.
    sound_enabled=true; settings_init();
    assert(!sound_enabled && settings_current()->light==SETTINGS_LIGHT_LOW);
    assert(settings_apply_display() && test_contrast==0x10);
    open_app("settings");
    handle_button_press(2); // Brightness selected.
    test_screen_ok=false; handle_button_press(1);
    assert(settings_current()->light==SETTINGS_LIGHT_LOW && !strcmp(settings_feedback,"SCREEN ERR"));
    test_screen_ok=true; handle_button_press(0); handle_button_press(1); // Sound on.
    assert(sound_enabled);
    handle_button_press(0); handle_button_press(1); // Wrap to SAVE EXIT.
    assert(!app_current());
    sound_enabled=false; settings_init(); assert(sound_enabled);
    capture_complete=false;
}

static void sample_xyz(int16_t x,int16_t y,int16_t z) {
    AppSample sample={.raw={x,y,z},.corrected={x,y,z},.filtered={x,y,z},.time_ms=test_now};
    app_publish_sample(&sample);
}

static void test_lifecycle(void) {
    for (size_t i=0;i<app_entry_count;++i) {
        assert(app_open(i));
        unsigned before=test_frames;
        app_draw(); assert(test_frames>before);
        app_return_home();
        assert(!app_current() && ui_state==UI_HOME && home_menu.selected==i);
    }
    assert(!app_open(app_entry_count));
    open_app("raw"); sample_xyz(1200,-2300,16000);
    assert(app_sample_current()->corrected[1]==-2300 && app_sample_period_ms()==100);
    app_button(0); assert(app_current()==&raw_app);
    app_button(1); assert(!app_current() && home_menu.selected==test_app_index("raw"));
    open_app("level"); assert(app_sample_period_ms()==100); app_button(1); assert(!app_current());
    open_app("acceleration"); assert(app_sample_period_ms()==100); app_button(1); assert(!app_current());
}

static void test_gyro_lifecycle(void) {
    unsigned stops=test_gyro_stops;
    open_app("gyro");
    assert(app_current()->fast_tick && !app_current()->owns_imu && gyro_peak_view);
    for (unsigned i=0;i<400;++i) { test_now+=5; app_tick(); }
    assert(gyro_session.phase==GYRO_RUNNING);
    test_gyro[0]=1000; test_now+=5; app_tick();
    assert(gyro_session.peak_mdps[0]==70000);
    app_button(0); assert(!gyro_peak_view);
    app_button(1); assert(gyro_session.peak_mdps[0]==0 && gyro_session.phase==GYRO_RESET_WAIT);
    app_button(2); assert(!app_current() && test_gyro_stops==stops+1);
    test_gyro[0]=0;
}

static void test_friction_controls(void) {
    open_app("friction");
    for (unsigned trial=0;trial<5;++trial) {
        app_button(1); assert(friction_state==FRICTION_ARMED && app_sample_period_ms()==20);
        test_now+=2500;
        for (unsigned i=0;i<10;++i) { test_now+=20; sample_xyz(0,0,16384); }
        for (unsigned i=0;i<2;++i) { test_now+=20; sample_xyz(9000,0,13600); }
        assert(friction_trial_count==trial+1);
        assert(friction_state==(trial==4 ? FRICTION_SUMMARY : FRICTION_RESULT));
    }
    friction_summary_selection=2; app_button(1);
    assert(friction_trial_count==4);
    friction_summary_selection=4; app_button(1); assert(friction_state==FRICTION_ARMED);
    app_button(0); assert(app_current()==&friction_app);
    app_button(1); assert(friction_state==FRICTION_READY);
    app_button(0); assert(!app_current() && home_menu.selected==test_app_index("friction"));
}

static void test_pendulum_controls(void) {
    open_app("pendulum"); app_button(1);
    assert(pendulum_state==PENDULUM_WAITING && app_sample_period_ms()==20);
    test_now+=2500;
    for (unsigned i=0;i<350 && pendulum_state!=PENDULUM_RESULT;++i) {
        test_now+=20;
        sample_xyz((int16_t)(10000+4000*cosf(i*0.125663706f)),0,0);
    }
    assert(pendulum_state==PENDULUM_RESULT && pendulum_period_count==4);
    assert(pendulum_period_total_ms>=3960 && pendulum_period_total_ms<=4040);
    app_button(1); assert(pendulum_state==PENDULUM_WAITING);
    app_button(1); assert(pendulum_state==PENDULUM_READY);
    app_button(0); assert(!app_current() && home_menu.selected==test_app_index("pendulum"));
}

static void test_capture_controls(void) {
    capture_complete=false;
    open_app("record");
    handle_button_press(1); assert(ui_state==UI_DROP_TEST);
    handle_button_press(1); assert(capture_active && capture_state==CAPTURE_WAIT_STILL);
    assert(!app_open(test_app_index("maze")));
    app_return_home(); assert(app_current()==&record_app);
    for (unsigned b=0;b<3;++b) handle_button_press(b);
    assert(capture_active && app_current()==&record_app);
    // Completed capture: options require NO/YES confirmation and preserve data
    // on the default NO path, across apps, and through the Gentle Catch game.
    capture_active=false; capture_complete=true; capture_count=19200;
    ui_state=UI_CAPTURE_COMPLETE;
    memset(capture_samples,0xa5,sizeof capture_samples);
    memset(capture_high_samples,0x5a,sizeof capture_high_samples);
    capture_metadata.calibration_generation=23;
    handle_button_press(1); assert(ui_state==UI_CAPTURE_OPTIONS);
    handle_button_press(1); assert(ui_state==UI_ERASE_CONFIRM && ui_selection==0);
    handle_button_press(1); assert(ui_state==UI_CAPTURE_OPTIONS && capture_complete && capture_count==19200);
    app_return_home(); open_app("maze"); app_return_home();
    assert(capture_complete && capture_count==19200 && capture_metadata.calibration_generation==23);
    open_app("record"); handle_button_press(1); handle_button_press(1);
    assert(!capture_active && capture_complete && capture_count==19200);
    app_return_home();
    capture_is_beam=true; capture_scale=8192; capture_rate=480;
    open_app("catch"); handle_button_press(1);
    assert(capture_active && ui_state==UI_GENTLE_CATCH && capture_complete && capture_count==19200);
    handle_button_press(0); assert(capture_active);
    handle_button_press(1); assert(!capture_active && ui_state==UI_CHALLENGES);
    assert(capture_scale==8192 && capture_rate==480 && capture_is_beam);
    for (size_t i=0;i<sizeof capture_samples;++i) assert(((uint8_t*)capture_samples)[i]==0xa5);
    for (size_t i=0;i<sizeof capture_high_samples;++i) assert(((uint8_t*)capture_high_samples)[i]==0x5a);
    handle_button_press(2); handle_button_press(1); assert(!app_current());
    open_app("beam"); assert(beam_state==BEAM_RESULT);
    handle_button_press(1); handle_button_press(1); assert(ui_state==UI_ERASE_CONFIRM);
    handle_button_press(2); handle_button_press(1); // Explicit YES.
    assert(!capture_complete && capture_count==0 && !app_current());
    assert(home_menu.selected==test_app_index("beam"));
    open_app("beam"); handle_button_press(1); assert(capture_active && beam_state==BEAM_WAITING);
    handle_button_press(1); assert(!capture_active && beam_state==BEAM_READY);
    handle_button_press(1); beam_state=BEAM_RECORDING;
    handle_button_press(1); assert(capture_active); // Cannot stop a triggered Beam.
    capture_active=false; beam_state=BEAM_READY; handle_button_press(0); assert(!app_current());
}

static void test_hammer_controls(void) {
    capture_active=capture_complete=capture_is_hammer=false;
    open_app("hammer");
    assert(app_current()==&hammer_app && ui_state==UI_HAMMER_HIT);
    handle_button_press(1);
    assert(capture_active && capture_is_hammer && hammer_gyro_running);
    assert(!app_open(test_app_index("maze")));
    app_return_home(); assert(app_current()==&hammer_app);
    handle_button_press(0); assert(capture_active);
    handle_button_press(1);
    assert(!capture_active && !hammer_gyro_running && !capture_count);
    handle_button_press(0); assert(!app_current() && home_menu.selected==test_app_index("hammer"));
    open_app("hammer"); test_gyro_ok=false; handle_button_press(1);
    assert(!capture_active && !hammer_gyro_running);
    test_gyro_ok=true; handle_button_press(1);
    hammer.phase=HAMMER_RECORDING;
    for (unsigned b=0;b<3;++b) handle_button_press(b);
    assert(capture_active && app_current()==&hammer_app);
    // Simulate completion, then navigate other apps without changing the saved
    // acquisition identity, calibration or samples. BEGIN cannot overwrite it.
    capture_active=false; capture_complete=true; capture_count=19200;
    capture_metadata.calibration_generation=71; ui_state=UI_CAPTURE_COMPLETE;
    memset(capture_samples,0x63,sizeof capture_samples);
    app_return_home(); open_app("maze"); app_return_home(); open_app("hammer");
    assert(ui_state==UI_HAMMER_HIT);
    handle_button_press(1); assert(ui_state==UI_CAPTURE_OPTIONS);
    handle_button_press(1); assert(ui_state==UI_ERASE_CONFIRM && ui_selection==0);
    handle_button_press(1); assert(capture_complete && capture_count==19200);
    assert(capture_metadata.calibration_generation==71);
    for(size_t i=0;i<sizeof capture_samples;++i) assert(((uint8_t*)capture_samples)[i]==0x63);
    handle_button_press(1); handle_button_press(2); handle_button_press(1);
    assert(!capture_complete && !capture_is_hammer && !app_current());
    // A saved non-Hammer capture opens its existing options, never a misleading
    // Hammer summary or a fresh listener.
    capture_complete=true; capture_is_beam=true; capture_count=7200;
    open_app("hammer"); assert(ui_state==UI_CAPTURE_OPTIONS && !capture_active);
    app_return_home(); capture_complete=capture_is_beam=false;
}

int main(void) {
    oled_ok=true; ui_set_display_available(true); ui_set_power(3900,true);
    settings_init();
    test_lifecycle(); test_gyro_lifecycle(); test_friction_controls();
    test_pendulum_controls(); test_capture_controls(); test_hammer_controls(); test_settings_controls();
    puts("App integration: every entry, lifecycle, controls, timing, capture protection and confirmation passed.");
    return 0;
}
