#include <assert.h>
#include "app_fixture.h"

static void open_app(const char *id) {
    capture_active=false;
    assert(app_open(test_app_index(id)));
    assert(app_current()==app_entries[test_app_index(id)].app);
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

int main(void) {
    oled_ok=true; ui_set_display_available(true); ui_set_power(3900,true);
    test_lifecycle(); test_gyro_lifecycle(); test_friction_controls();
    test_pendulum_controls(); test_capture_controls();
    puts("App integration: every entry, lifecycle, controls, timing, capture protection and confirmation passed.");
    return 0;
}
