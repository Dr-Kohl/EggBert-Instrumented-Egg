#include "app_fixture.h"
static void fresh_home(void) {
    idle_standby=false; idle_wake_buttons=false;
    capture_active=orientation_stream=orientation_fused=false;
    app_return_home();
    test_usb_connected=false; test_idle_ok=test_wake_ok=true;
    idle_power_init(&idle_power,test_now);
}
int main(void) {
    ssd1306_init(); oled_ok=true; ui_set_display_available(true);
    fresh_home();
    // Byte-for-byte protected data and pending calibration survive standby/wake.
    capture_complete=true; capture_count=123; capture_has_highg=true;
    capture_state=CAPTURE_COMPLETE;
    capture_metadata.calibration_generation=27;
    memset(capture_samples,0x35,sizeof capture_samples);
    memset(capture_high_samples,0x57,sizeof capture_high_samples);
    home_menu.selected=test_app_index("hammer");
    test_now+=IDLE_TIMEOUT_MS-1; service_idle_power(false); assert(!idle_standby);
    test_now++; service_idle_power(false); assert(idle_standby && !ui_display_available());
    unsigned frames=test_frames;
    assert(test_led_mask==4); // first reminder pulse begins at standby entry
    test_now+=99; update_status_leds(true); assert(test_led_mask==4);
    test_now++; update_status_leds(true); assert(test_led_mask==0);
    test_now+=4899; update_status_leds(true); assert(test_led_mask==0);
    test_now++; update_status_leds(true); assert(test_led_mask==4);
    test_now+=100; update_status_leds(true); assert(test_led_mask==0);
    assert(test_frames==frames); // blinking never wakes or redraws the OLED
    capture_complete=false; update_status_leds(true); assert(test_led_mask==0);
    capture_complete=true; capture_count=0;
    test_now=idle_started_ms+5000; update_status_leds(true); assert(test_led_mask==0);
    capture_count=123;
    // Unsigned elapsed timing also works across millisecond-clock rollover.
    idle_started_ms=UINT32_MAX-49u; test_now=25;
    update_status_leds(true); assert(test_led_mask==4);
    test_now=50; update_status_leds(true); assert(test_led_mask==0);
    show_ui(); assert(test_frames==frames);
    handle_button_press(1); assert(!idle_standby && !app_current());
    assert(test_led_mask==4); // awake capture-complete indication is solid red
    // Other buttons pressed in the same waking chord cannot open/erase/arm.
    handle_button_press(0); handle_button_press(1); assert(!app_current());
    assert(capture_complete && capture_count==123 && capture_has_highg);
    assert(capture_metadata.calibration_generation==27);
    for(size_t i=0;i<sizeof capture_samples;i++) assert(((uint8_t*)capture_samples)[i]==0x35);
    for(size_t i=0;i<sizeof capture_high_samples;i++) assert(((uint8_t*)capture_high_samples)[i]==0x57);
    idle_wake_buttons=false; // physical all-buttons-released state
    // A held button, active acquisition, streams and an open USB host inhibit idle.
    for(unsigned reason=0;reason<5;reason++) {
        fresh_home(); test_now+=IDLE_TIMEOUT_MS;
        capture_active=reason==0; orientation_fused=reason==1;
        orientation_stream=reason==2; test_usb_connected=reason==3;
        service_idle_power(reason==4); assert(!idle_standby);
    }
    fresh_home(); capture_complete=false;
    const char *active[]={"pendulum","friction","calibrate","maze","gyro","beam"};
    for(unsigned i=0;i<6;i++) {
        assert(app_open(test_app_index(active[i])));
        if(i!=4) app_button(1);
        test_now+=IDLE_TIMEOUT_MS; service_idle_power(false); assert(!idle_standby);
        capture_active=false; app_return_home();
    }
    // Failed hardware transitions must not falsely report a successful sleep/wake.
    fresh_home(); test_idle_ok=false; test_now+=IDLE_TIMEOUT_MS;
    service_idle_power(false); assert(!idle_standby && ui_display_available());
    test_idle_ok=true; test_now+=IDLE_TIMEOUT_MS; service_idle_power(false); assert(idle_standby);
    test_wake_ok=false; handle_button_press(1); assert(idle_standby);
    test_wake_ok=true; handle_button_press(1); assert(!idle_standby);
    fresh_home(); capture_complete=false;
    test_now+=IDLE_TIMEOUT_MS; service_idle_power(false);
    assert(idle_standby && test_led_mask==0);
    test_now+=5000; update_status_leds(true); assert(test_led_mask==0);
    assert(wake_idle());
    assert(app_open(test_app_index("acceleration")));
    test_now+=IDLE_TIMEOUT_MS; service_idle_power(false); assert(idle_standby);
    assert(wake_idle() && app_current()==&acceleration_app);
    // Timer wakes service only the recording reminder; motion/USB wake the
    // same view. Short button IRQs are consumed even if released before polling.
    for (unsigned reason=0;reason<4;reason++) {
        fresh_home(); capture_complete=true; capture_count=123;
        test_now+=IDLE_TIMEOUT_MS; service_idle_power(false);
        assert(idle_standby && test_sleep_begins>0);
        test_sleep_reason=0; service_idle_sleep();
        assert(idle_standby && test_sleep_delay==100);
        test_now+=100; service_idle_sleep();
        assert(idle_standby && test_sleep_delay==4900);
        test_sleep_reason=reason==0 ? PROCESSOR_WAKE_MOTION : reason==1 ? PROCESSOR_WAKE_USB :
                          reason==2 ? PROCESSOR_WAKE_BUTTON : PROCESSOR_WAKE_BUTTON|PROCESSOR_WAKE_MOTION;
        service_idle_sleep(); assert(!idle_standby && test_sleep_ends>0);
        assert(idle_wake_buttons==(reason>=2));
        assert(!app_current() && capture_count==123 && capture_complete);
        for(size_t i=0;i<sizeof capture_samples;i++) assert(((uint8_t*)capture_samples)[i]==0x35);
    }
    return 0;
}
