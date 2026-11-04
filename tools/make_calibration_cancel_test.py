"""Generate a native regression harness using the actual App and capture modules.

Run tools/run_native_tests.py to generate, compile and execute all harnesses.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
test=r'''

#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static void six_faces(void) {
    calibration_reset_wizard(); app_open(test_app_index("calibrate")); test_time_mode=1;
    for (unsigned face=0; face<6; ++face) {
        calibration_begin_face();
        int16_t xyz[3]={0}; xyz[face/2]=(face&1) ? -16000 : 16000;
        for (unsigned i=0; i<CALIBRATION_SAMPLE_COUNT; ++i)
            calibration_process_raw(xyz[0],xyz[1],xyz[2]);
    }
}
int calibration_cancel_test(void) {
    oled_ok=true; ui_set_display_available(true);
    test_calibration=(calibration_record_t){.generation=7,.offset_live_counts={33,-22,11},.scale_q16={65536,65536,65536}};
    calibration_valid=true;
    calibration_record_t original=test_calibration;
    calibration_reset_wizard(); app_open(test_app_index("calibrate"));
    calibration_exit();
    CHECK(ui_state==UI_HOME && home_menu.selected==test_app_index("calibrate") && test_saves==0);
    CHECK(memcmp(&original,&test_calibration,sizeof original)==0);
    // Cancel during the set-down delay; delayed samples cannot resume collection.
    app_open(test_app_index("calibrate")); test_time_mode=0; calibration_begin_face();
    calibration_process_raw(16000,0,0); CHECK(calibration_armed);
    calibration_exit(); test_time_mode=1; calibration_process_raw(16000,0,0);
    CHECK(!calibration_armed && !calibration_collecting && test_saves==0);
    // Cancel in the middle of averaging and after a completed partial face.
    app_open(test_app_index("calibrate")); calibration_begin_face(); calibration_process_raw(16000,0,0);
    CHECK(calibration_collecting); calibration_exit();
    for (unsigned i=0; i<40; ++i) calibration_process_raw(16000,0,0);
    CHECK(!calibration_collecting && calibration_faces_seen==0 && test_saves==0);
    app_open(test_app_index("calibrate")); calibration_begin_face();
    for (unsigned i=0;i<CALIBRATION_SAMPLE_COUNT;++i) calibration_process_raw(16000,0,0);
    CHECK(calibration_faces_seen==1); calibration_exit();
    CHECK(memcmp(&original,&test_calibration,sizeof original)==0 && test_saves==0);
    // All six faces stage a candidate; they neither activate nor persist it.
    six_faces();
    CHECK(calibration_faces_seen==63 && calibration_ready_to_save && !calibration_complete);
    CHECK(memcmp(&original,&test_calibration,sizeof original)==0 && test_saves==0);
    calibration_exit(); calibration_commit_pending();
    CHECK(!calibration_ready_to_save && test_saves==0);
    CHECK(memcmp(&original,&test_calibration,sizeof original)==0);
    // Explicit save failure preserves the active record and allows cancellation.
    six_faces(); test_save_ok=false; calibration_commit_pending();
    CHECK(test_saves==1 && calibration_ready_to_save && !calibration_complete);
    CHECK(memcmp(&original,&test_calibration,sizeof original)==0); calibration_exit();
    // Only explicit successful save installs the staged test_calibration.
    six_faces(); test_save_ok=true; calibration_commit_pending();
    CHECK(test_saves==2 && calibration_complete && !calibration_ready_to_save);
    CHECK(test_calibration.generation==8 && test_calibration.offset_live_counts[0]==0);
    calibration_record_t saved=test_calibration; calibration_exit();
    CHECK(memcmp(&saved,&test_calibration,sizeof saved)==0);
    // Invalid later-axis span must not partially modify the active record.
    app_open(test_app_index("calibrate")); calibration_faces_seen=31;
    calibration_positive[0]=16000; calibration_negative[0]=-16000;
    calibration_positive[1]=10000; calibration_negative[1]=-10000;
    calibration_positive[2]=16000;
    calibration_sum[0]=calibration_sum[1]=0;
    calibration_sum[2]=-16000*(int64_t)CALIBRATION_SAMPLE_COUNT;
    for (unsigned i=0;i<3;++i) calibration_minimum[i]=calibration_maximum[i]=0;
    calibration_finish_face();
    CHECK(!calibration_ready_to_save && test_saves==2);
    CHECK(memcmp(&saved,&test_calibration,sizeof saved)==0);
    return 0;
}
'''
output=root/'build/calibration-cancel-test.c'
output.parent.mkdir(exist_ok=True)
output.write_text('#include "../tests/app_fixture.h"\n'+test)
print(output)
