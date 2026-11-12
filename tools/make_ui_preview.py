"""Generate native previews of the actual production App, Home and OLED code.

Run tools/run_native_tests.py for generation, compilation and rendering.
"""
import json
import struct
import sys
import zlib
from pathlib import Path
root=Path(__file__).resolve().parents[1]
cases = [('hammer-ready', 'show_hammer();'),
 ('hammer-turn', 'capture_active=true; hammer_reset(&hammer); show_hammer(); for(unsigned i=0;i<8;++i) ssd1306_service_show();'),
 ('hammer-reverse', 'hammer.phase=HAMMER_REVERSE; hammer.turns=1; show_hammer(); for(unsigned i=0;i<8;++i) ssd1306_service_show();'),
 ('hammer-pause', 'hammer.phase=HAMMER_FINAL_SETTLE; hammer.turns=2; hammer.quiet_us=1500000; show_hammer(); for(unsigned i=0;i<8;++i) ssd1306_service_show();'),
 ('hammer-armed', 'hammer.phase=HAMMER_ARMED; show_hammer(); for(unsigned i=0;i<8;++i) ssd1306_service_show();'),
 ('hammer-recording', 'hammer.phase=HAMMER_RECORDING; show_hammer(); for(unsigned i=0;i<8;++i) ssd1306_service_show(); capture_active=false;'),
 ('hammer-done', 'capture_complete=true; trigger_detected=true; capture_count=19200; hammer_peak_mg=31251; capture_has_highg=true; hammer_over20_samples=48; show_hammer(); capture_complete=false;'),
('home', 'home_menu=(HomeMenu){0}; home_draw();'),
 ('home-2',
  'home_menu=(HomeMenu){0}; for(unsigned i=0;i<9;++i) home_menu_move(&home_menu,app_entry_count,true); '
  'home_draw();'),
 ('home-maze', 'home_menu_select(&home_menu,app_entry_count,test_app_index("maze")); home_draw();'),
 ('battery-v1',
  'ui_set_power(3900,false); test_model=LSM6DSV_MODEL_LSM6DSV; home_menu=(HomeMenu){0}; home_draw(); '
  'ui_set_power(3900,true); test_model=LSM6DSV_MODEL_LSM6DSV320X;'),
 ('accel', 'show_acceleration();'),
 ('raw', 'show_raw_xyz();'),
 ('level', 'show_inclinometer();'),
 ('gyro-zero', 'gyro_ok=true; gyro_session.phase=GYRO_ZEROING; show_gyro();'),
 ('gyro-peak',
  'gyro_session.phase=GYRO_RUNNING; gyro_session.peak_mdps[0]=2000000; gyro_session.peak_mdps[1]=-1500000; '
  'gyro_session.peak_mdps[2]=500000; show_gyro();'),
 ('record-menu', 'ui_selection=0; show_record_menu();'),
 ('drop-menu', 'show_drop_test_menu();'),
 ('catch-menu', 'show_challenges_menu();'),
 ('record-arming', 'show_capture_wait_still();'),
 ('record-ready', 'show_capture_armed();'),
 ('record-event', 'trigger_was_freefall=true; show_capture_event();'),
 ('record-done', 'show_capture_complete();'),
 ('saved-options', 'show_capture_options();'),
 ('erase', 'ui_selection=1; show_erase_confirm();'),
 ('catch-ready', 'show_gentle_ready();'),
 ('catch-fall', 'show_gentle_fall();'),
 ('catch-measure', 'show_gentle_measure();'),
 ('catch-result', 'gentle_peak_axis_counts=204595; show_gentle_result();'),
 ('catch-320', 'gentle_peak_axis_counts=655360; show_gentle_result();'),
 ('catch-clipped', 'gentle_has_highg=true; gentle_saturated=true; show_gentle_result();'),
 ('calibrate', 'show_calibration();'),
 ('calibrate-delay', 'calibration_armed=true; show_calibration(); calibration_armed=false;'),
 ('calibrate-capture', 'calibration_collecting=true; show_calibration(); calibration_collecting=false;'),
 ('calibrate-ready', 'calibration_faces_seen=63; calibration_ready_to_save=true; show_calibration();'),
 ('calibrate-error',
  'strcpy(calibration_feedback,"SAVE FAIL"); show_calibration(); calibration_ready_to_save=false;'),
 ('calibrate-saved', 'calibration_complete=true; show_calibration();'),
 ('friction-ready', 'friction_state=FRICTION_READY; show_friction();'),
 ('friction-wait', 'friction_state=FRICTION_ARMED; friction_waiting_to_arm=true; show_friction();'),
 ('friction-tilt', 'friction_waiting_to_arm=false; friction_last_stable_angle=34.5f; show_friction();'),
 ('friction-result', 'friction_state=FRICTION_RESULT; show_friction();'),
 ('friction-trials', 'friction_state=FRICTION_SUMMARY; friction_summary_selection=4; show_friction();'),
 ('friction-rearm', 'friction_summary_selection=5; show_friction();'),
 ('friction-exit', 'friction_summary_selection=6; show_friction();'),
 ('pendulum-ready', 'pendulum_state=PENDULUM_READY; show_pendulum();'),
 ('pendulum-wait', 'pendulum_state=PENDULUM_WAITING; show_pendulum();'),
 ('pendulum-swing', 'pendulum_state=PENDULUM_MEASURING; show_pendulum();'),
 ('pendulum-weak', 'pendulum_state=PENDULUM_WEAK; show_pendulum();'),
 ('pendulum-result', 'pendulum_state=PENDULUM_RESULT; pendulum_detector.interval_count=6; show_pendulum();'),
 ('beam-ready', 'beam_state=BEAM_READY; show_beam();'),
 ('beam-wait', 'beam_state=BEAM_WAITING; show_beam();'),
 ('beam-listen', 'beam_state=BEAM_LISTENING; show_beam();'),
 ('beam-record',
  'beam_state=BEAM_RECORDING; capture_target=BEAM_TOTAL_SAMPLES; capture_pretrigger=BEAM_PRE_SAMPLES; '
  'show_beam();'),
 ('beam-result', 'beam_state=BEAM_RESULT; beam_result_hz=4.81f; show_beam();'),
 ('beam-no-period', 'beam_result_hz=0; capture_clipped=true; show_beam();'),
 ('maze-ready', 'maze_generate(&maze_game,12345); maze_phase=MAZE_READY; show_maze();'),
 ('maze-centering', 'maze_phase=MAZE_CENTERING; show_maze();'),
 ('maze-playing', 'maze_phase=MAZE_PLAYING; show_maze();'),
 ('maze-scroll', 'maze_game.x=78; maze_game.y=114; maze_scroll(&maze_game); show_maze();'),
 ('maze-pause', 'maze_phase=MAZE_PAUSED; maze_menu_selection=0; show_maze();'),
 ('maze-exit', 'maze_menu_selection=4; show_maze();'),
 ('maze-finished', 'maze_phase=MAZE_FINISHED; maze_elapsed_ms=123000; show_maze();'),
 ('maze-error', 'maze_phase=MAZE_ERROR; show_maze();'),
 ('home-scroll-trigger',
  'home_menu=(HomeMenu){0}; for(unsigned i=0;i<4;++i) home_menu_move(&home_menu,app_entry_count,true); '
  'home_draw();'),
 ('home-scroll-up', 'home_menu_move(&home_menu,app_entry_count,false); home_draw();'),
 ('home-wrap-last',
  'home_menu=(HomeMenu){0}; home_menu_move(&home_menu,app_entry_count,false); home_draw();'),
 ('settings', 'settings_enter(); show_settings();'),
 ('settings-muted', 'settings_set_sound(false); show_settings();'),
 ('settings-bright', 'settings_selection=1; settings_set_light(SETTINGS_LIGHT_HIGH); show_settings();'),
 ('settings-exit', 'settings_selection=2; show_settings();'),
 ('settings-save-error', 'settings_feedback="SAVE ERROR"; show_settings();')]
program = r'''
#include "../tests/app_fixture.h"
int ui_preview(void) {
    oled_ok=true; ui_set_display_available(true); ui_set_power(3900,true);
    settings_init();
    AppSample preview_sample={.corrected={-32768,16384,1234},.filtered={1500,-2500,16000}};
    app_publish_sample(&preview_sample);
    friction_trial_count=5;
    for(unsigned i=0;i<5;++i) { friction_trial_angles[i]=25.5f+i; friction_trial_mu[i]=0.5f+i*0.1f; }
    pendulum_detector.interval_count=3; pendulum_detector.period_seconds=2.0f;
''' + '\n'.join(code for _, code in cases) + r'''
    return test_text_overflows ? 100+(int)test_text_overflows : 0;
}
int main(void) {
    test_frame_output=fopen("build/ui-preview.bin","wb");
    if(!test_frame_output) return 1;
    int result=ui_preview();
    if(fclose(test_frame_output)) return 1;
    if(test_frames!=EXPECTED_FRAMES) return 2;
    printf("UI preview: %u states, %u text overflows.\n",test_frames,test_text_overflows);
    return result;
}
'''
program = program.replace('EXPECTED_FRAMES', str(len(cases)))

if "--render" not in sys.argv:
    (root/'build/ui-preview.c').write_text(program)
    graphics=(root/'src/ssd1306.c').read_text()
    graphics='#include "../tests/app_platform.h"\n'+graphics
    for name,scale,height in [('ssd1306_ui_text','1u','1u'),('ssd1306_ui_text_double_height','1u','2u'),('ssd1306_ui_text_scaled','scale','scale')]:
        start=graphics.index('void '+name+'(')
        opening=graphics.index('{',start)+1
        check='\n    if(strlen(text) && (x+(strlen(text)*6u-1u)*'+scale+'>64u || y+7u*'+height+'>128u)) ++test_text_overflows;'
        graphics=graphics[:opening]+check+graphics[opening:]
    (root/'build/ui-graphics-preview.c').write_text(graphics)
    print(root/'build/ui-preview.c')
else:
    frames = (root/'build/ui-preview.bin').read_bytes()
    output = root/'build/ui-preview'
    output.mkdir(parents=True,exist_ok=True)
    def png(path,width,height,pixels):
        def chunk(kind,data):
            return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        rows=b''.join(b'\0'+pixels[y*width:(y+1)*width] for y in range(height))
        path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    logical=[]
    for i,(name,_) in enumerate(cases):
        frame=frames[i*1024:(i+1)*1024]
        image=bytes(255 if frame[(x//8)*128+127-y]&(1<<(x%8)) else 0 for y in range(128) for x in range(64))
        logical.append(image)
        expanded=bytes(image[(y//3)*64+x//3] for y in range(384) for x in range(192))
        png(output/(name+'.png'),192,384,expanded)
    # Galleries keep each actual framebuffer visible at 2x, with no resampling.
    for start in range(0,len(cases),12):
        width,height=4*136,3*276
        pixels=bytearray([35])*(width*height)
        for n,image in enumerate(logical[start:start+12]):
            left=(n%4)*136+4; top=(n//4)*276+10
            for y in range(256):
                for x in range(128):pixels[(top+y)*width+left+x]=image[(y//2)*64+x//2]
        png(output/f'gallery-{start//12+1}.png',width,height,pixels)
    (output/'screens.json').write_text(json.dumps([name for name,_ in cases],indent=2)+'\n')
    print(output)
