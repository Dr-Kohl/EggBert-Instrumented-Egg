"""Generate a native regression harness using the actual App and capture modules.

Run tools/run_native_tests.py to generate, compile and execute all harnesses.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
test=r'''

static void advance(unsigned ticks) {
    for (unsigned i=0;i<ticks;++i) { test_now+=10; service_maze(); }
}
int main(void) {
    oled_ok=true; ui_set_display_available(true);
    memset(capture_samples,0xa5,sizeof capture_samples);
    memset(capture_high_samples,0x5a,sizeof capture_high_samples);
    memset(&capture_metadata,0x55,sizeof capture_metadata);
    egg2_metadata_t saved_metadata=capture_metadata;
    memset(&calibration,3,sizeof calibration);
    calibration_record_t saved_calibration=calibration;
    capture_complete=true; capture_is_beam=true;
    capture_rate=480; capture_scale=8192; capture_count=7200;
    capture_write_index=7422; trigger_index=14;
    capture_state=CAPTURE_COMPLETE;
    test_now=1000; app_open(test_app_index("maze"));
    maze_new_game();
    assert(maze_phase==MAZE_READY);
    uint8_t layout[MAZE_CELLS]; memcpy(layout,maze_game.walls,sizeof layout);
    maze_handle_button(1); assert(maze_phase==MAZE_CENTERING);
    advance(40); assert(maze_phase==MAZE_CENTERING); // Button settling delay.
    advance(140); assert(maze_phase==MAZE_PLAYING);
    assert(maze_neutral_right==0 && maze_neutral_down==0);
    assert(maze_game.x==6 && maze_game.y==6 && test_frames>0);
    test_accel[1]=5000; advance(30);
    assert(maze_filtered_right>0.29f && fabsf(maze_filtered_down)<0.001f);
    test_accel[1]=0; test_accel[0]=5000; advance(30);
    assert(maze_filtered_down > 0.29f); // Physical game check: +X drives down.
    maze_handle_button(1); assert(maze_phase==MAZE_PAUSED);
    float x=maze_game.x,y=maze_game.y; uint32_t elapsed=maze_elapsed_ms;
    advance(1000);
    assert(maze_game.x==x && maze_game.y==y && maze_elapsed_ms==elapsed);
    maze_menu_selection=0; maze_handle_button(1); advance(1);
    assert(maze_phase==MAZE_PLAYING && maze_elapsed_ms==elapsed+10);
    maze_handle_button(1); maze_menu_selection=1; maze_handle_button(1);
    assert(!memcmp(layout,maze_game.walls,sizeof layout));
    assert(maze_game.x==6 && maze_elapsed_ms==0 && maze_phase==MAZE_CENTERING);
    // Reject face-down and moving samples; a stable screen-up tilted posture
    // establishes a temporary game neutral, leaving saved calibration intact.
    test_accel[0]=0; test_accel[1]=0; test_accel[2]=-16384; advance(200);
    assert(maze_phase==MAZE_CENTERING && maze_center_samples==0);
    test_accel[2]=16384;
    for (unsigned i=0;i<200;++i) { test_accel[1]=(i%2)*2000; advance(1); }
    assert(maze_phase==MAZE_CENTERING);
    test_accel[0]=800; test_accel[1]=1000; advance(150);
    assert(maze_phase==MAZE_PLAYING);
    assert(fabsf(maze_neutral_right-1000.0f/16384)<0.00001f);
    assert(fabsf(maze_neutral_down-800.0f/16384)<0.00001f);
    x=maze_game.x; y=maze_game.y; advance(100);
    assert(maze_game.x==x && maze_game.y==y);
    test_sensor_ok=false; advance(60);
    assert(maze_phase==MAZE_ERROR && maze_game.vx==0 && maze_game.vy==0);
    test_sensor_ok=true; maze_handle_button(1); advance(180);
    assert(maze_phase==MAZE_PLAYING);
    // Recenter preserves position and elapsed time; it does not restart play.
    maze_handle_button(1); elapsed=maze_elapsed_ms; x=maze_game.x; y=maze_game.y;
    maze_menu_selection=3; maze_handle_button(1); advance(120);
    assert(maze_phase==MAZE_PLAYING && maze_game.x==x && maze_game.y==y);
    assert(maze_elapsed_ms>=elapsed && maze_elapsed_ms<=elapsed+100);
    maze_handle_button(1); maze_menu_selection=2; maze_handle_button(1);
    assert(maze_phase==MAZE_READY && maze_elapsed_ms==0);
    assert(memcmp(layout,maze_game.walls,sizeof layout));
    maze_phase=MAZE_FINISHED; maze_handle_button(1);
    assert(maze_phase==MAZE_READY);
    maze_handle_button(2); assert(!app_current() && home_menu.selected==test_app_index("maze"));
    assert(capture_complete && capture_is_beam && !capture_active);
    assert(capture_count==7200 && capture_rate==480 && capture_scale==8192);
    assert(capture_write_index==7422 && trigger_index==14 && capture_state==CAPTURE_COMPLETE);
    assert(!memcmp(&capture_metadata,&saved_metadata,sizeof capture_metadata));
    assert(!memcmp(&calibration,&saved_calibration,sizeof calibration));
    for (size_t i=0;i<sizeof capture_samples;++i) assert(((uint8_t*)capture_samples)[i]==0xa5);
    for (size_t i=0;i<sizeof capture_high_samples;++i) assert(((uint8_t*)capture_high_samples)[i]==0x5a);
    puts("Maze session tests passed: controls, neutral, pause, timeout, navigation, protected capture.");
    return 0;
}

'''
output=root/'build/maze-session-test.c'
output.parent.mkdir(exist_ok=True)
output.write_text('#include "../tests/app_fixture.h"\n'+test)
print(output)
