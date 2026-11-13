#include <stdio.h>
#include <string.h>
#include <math.h>
#include "app.h"
#include "ui.h"
#include "calibration.h"
#include "pico/stdlib.h"
#include "maze.h"
#include "lsm6dsv.h"
#include "sound.h"

typedef enum { MAZE_READY, MAZE_CENTERING, MAZE_PLAYING, MAZE_PAUSED,
               MAZE_FINISHED, MAZE_ERROR } maze_phase_t;
static maze_t maze_game;
static maze_phase_t maze_phase;
static unsigned maze_menu_selection;
static uint32_t maze_next_sample_ms, maze_next_display_ms, maze_last_tick_ms;
static uint32_t maze_last_sample_ms, maze_center_after_ms, maze_center_started_ms;
static uint32_t maze_elapsed_ms;
static unsigned maze_center_samples;
static int32_t maze_center_sum[3];
static int16_t maze_center_min[3], maze_center_max[3];
static float maze_neutral_right, maze_neutral_down, maze_filtered_right, maze_filtered_down;
static bool maze_filter_valid;

static void maze_draw_world_pixel(int x, int y, bool on) {
    x -= maze_game.camera_x;
    y -= maze_game.camera_y;
    if (x >= 0 && x < MAZE_VIEW_WIDTH && y >= 0 && y < MAZE_VIEW_HEIGHT)
        ssd1306_ui_pixel((uint8_t)x, (uint8_t)(y + 28), on);
}

static void maze_draw_bearing(void) {
    static const int8_t dx[8] = {0,1,1,1,0,-1,-1,-1};
    static const int8_t dy[8] = {-1,-1,0,1,1,1,0,-1};
    unsigned d = maze_goal_direction(&maze_game);
    int x = dx[d], y = dy[d], tip_x = 55 + 4*x, tip_y = 20 + 4*y;
    for (int i = -3; i <= 4; ++i)
        ssd1306_ui_pixel((uint8_t)(55+i*x), (uint8_t)(20+i*y), true);
    for (int i = 1; i <= 2; ++i) {
        ssd1306_ui_pixel((uint8_t)(tip_x-i*x-i*y), (uint8_t)(tip_y-i*y+i*x), true);
        ssd1306_ui_pixel((uint8_t)(tip_x-i*x+i*y), (uint8_t)(tip_y-i*y-i*x), true);
    }
}

static void show_maze(void) {
    if (!ui_display_available()) return;
    ui_begin("MAZE");
    if (maze_phase == MAZE_PLAYING) {
        maze_draw_bearing();
        for (int y = 0; y < MAZE_VIEW_HEIGHT; ++y)
            for (int x = 0; x < MAZE_VIEW_WIDTH; ++x)
                if (maze_wall_pixel(&maze_game, x + maze_game.camera_x, y + maze_game.camera_y))
                    ssd1306_ui_pixel((uint8_t)x, (uint8_t)(y + 28), true);
        int gx = (maze_game.goal % MAZE_COLS) * MAZE_CELL_PIXELS + MAZE_CELL_PIXELS/2;
        int gy = (maze_game.goal / MAZE_COLS) * MAZE_CELL_PIXELS + MAZE_CELL_PIXELS/2;
        for (int i = -2; i <= 2; ++i) {
            maze_draw_world_pixel(gx+i, gy-2, true); maze_draw_world_pixel(gx+i, gy+2, true);
            maze_draw_world_pixel(gx-2, gy+i, true); maze_draw_world_pixel(gx+2, gy+i, true);
        }
        int bx = (int)lroundf(maze_game.x), by = (int)lroundf(maze_game.y);
        for (int y = -1; y <= 1; ++y)
            for (int x = -1; x <= 1; ++x) maze_draw_world_pixel(bx+x, by+y, true);
        ssd1306_ui_text(2, 120, "MID PAUSE", true);
    } else if (maze_phase == MAZE_PAUSED) {
        ssd1306_ui_text(2, 30, "PAUSED", true);
        ui_menu_item(45, "RESUME", maze_menu_selection == 0);
        ui_menu_item(58, "RESTART", maze_menu_selection == 1);
        ui_menu_item(71, "NEW MAZE", maze_menu_selection == 2);
        ui_menu_item(84, "RECENTER", maze_menu_selection == 3);
        ui_menu_item(97, "EXIT", maze_menu_selection == 4);
        ssd1306_ui_text(2, 120, "MID SELECT", true);
    } else if (maze_phase == MAZE_CENTERING) {
        ssd1306_ui_text(2, 34, "SCREEN UP", true);
        ssd1306_ui_text(2, 48, "HOLD STILL", true);
        ssd1306_ui_text(2, 68, "CENTERING", true);
        ssd1306_ui_text(2, 120, "DN CANCEL", true);
    } else if (maze_phase == MAZE_FINISHED) {
        char text[12];
        ssd1306_ui_text(2, 34, "YOU WIN", true);
        snprintf(text, sizeof text, "%lu SEC", (unsigned long)(maze_elapsed_ms/1000u));
        ssd1306_ui_text(2, 52, text, true);
        ssd1306_ui_text(2, 82, "MID NEW", true);
        ssd1306_ui_text(2, 98, "DN EXIT", true);
    } else if (maze_phase == MAZE_ERROR) {
        ssd1306_ui_text(2, 34, "IMU ERROR", true);
        ssd1306_ui_text(2, 54, "GAME HELD", true);
        ssd1306_ui_text(2, 82, "MID RETRY", true);
        ssd1306_ui_text(2, 98, "DN EXIT", true);
    } else {
        ssd1306_ui_text(2, 32, "TILT TO", true);
        ssd1306_ui_text(2, 44, "ROLL BALL", true);
        ssd1306_ui_text(2, 62, "FIND GOAL", true);
        ssd1306_ui_text(2, 74, "ARROW AIMS", true);
        ssd1306_ui_text(2, 86, "AT GOAL", true);
        ssd1306_ui_text(2, 106, "MID START", true);
        ssd1306_ui_text(2, 120, "DN EXIT", true);
    }
    ssd1306_show();
}

static void maze_new_game(void) {
    // Button timing supplies variety; the generator itself is deterministic.
    uint32_t seed = (uint32_t)time_us_64() ^ (maze_game.seed * 1664525u + 1013904223u);
    maze_generate(&maze_game, seed);
    maze_elapsed_ms = 0;
    maze_phase = MAZE_READY;
    maze_menu_selection = 0;
    printf("Maze seed=%lu, route=%u cells, turns=%u, dead ends=%u.\n",
           (unsigned long)seed, maze_game.solution_steps, maze_game.solution_turns, maze_game.dead_ends);
}

static void maze_begin_centering(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    maze_phase = MAZE_CENTERING;
    maze_game.vx = maze_game.vy = 0;
    maze_center_samples = 0;
    maze_center_after_ms = now + 500u; // Let the START button disturbance settle.
    maze_next_sample_ms = maze_next_display_ms = maze_last_sample_ms = now;
    maze_filter_valid = false;
}

static void maze_resume(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    maze_phase = MAZE_PLAYING;
    maze_game.vx = maze_game.vy = 0;
    maze_last_tick_ms = maze_last_sample_ms = maze_next_sample_ms = maze_next_display_ms = now;
    maze_filter_valid = false;
}

static void maze_process_centering(const int16_t values[3], uint32_t now) {
    if ((int32_t)(now-maze_center_after_ms) < 0) return;
    float magnitude = (float)values[0]*values[0] + (float)values[1]*values[1] + (float)values[2]*values[2];
    const float g = LIVE_ACCEL_COUNTS_PER_G;
    // Screen-up near 1 g is an intentional playing posture, not a new saved
    // calibration. Movement restarts this temporary neutral measurement.
    if (values[2] < g*0.65f || magnitude < g*g*0.85f*0.85f || magnitude > g*g*1.15f*1.15f) {
        maze_center_samples = 0; return;
    }
    if (!maze_center_samples) {
        maze_center_started_ms = now;
        for (unsigned a = 0; a < 3; ++a) {
            maze_center_sum[a] = 0;
            maze_center_min[a] = maze_center_max[a] = values[a];
        }
    }
    for (unsigned a = 0; a < 3; ++a) {
        if (values[a] < maze_center_min[a]) maze_center_min[a] = values[a];
        if (values[a] > maze_center_max[a]) maze_center_max[a] = values[a];
        if (maze_center_max[a]-maze_center_min[a] > (int)(g*0.06f)) {
            maze_center_samples = 0; return;
        }
        maze_center_sum[a] += values[a];
    }
    ++maze_center_samples;
    if (maze_center_samples >= 40u && now-maze_center_started_ms >= 600u) {
        // Physical game check: +Y drives right and +X drives down.
        maze_neutral_right = maze_center_sum[1] / (g*maze_center_samples);
        maze_neutral_down = maze_center_sum[0] / (g*maze_center_samples);
        maze_resume();
        sound_play(SOUND_READY);
    }
}

static void service_maze(void) {
    if (maze_phase != MAZE_CENTERING && maze_phase != MAZE_PLAYING) return;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if ((int32_t)(now-maze_next_sample_ms) >= 0) {
        maze_next_sample_ms = now + 10u;
        int16_t raw[3];
        bool read = lsm6dsv_read_accel(&raw[0], &raw[1], &raw[2]);
        if (now-maze_last_sample_ms > 500u) {
            // Resume/recenter explicitly after a sensor or foreground stall.
            maze_phase = MAZE_ERROR; maze_game.vx = maze_game.vy = 0;
            sound_play(SOUND_ERROR); show_maze(); return;
        }
        if (read) {
            maze_last_sample_ms = now;
            for (unsigned a = 0; a < 3; ++a)
                raw[a] = calibration_correct_axis(raw[a], a, LIVE_ACCEL_COUNTS_PER_G);
            if (maze_phase == MAZE_CENTERING) maze_process_centering(raw, now);
            else {
                uint32_t elapsed = now-maze_last_tick_ms;
                maze_last_tick_ms = now;
                float right = (float)raw[1]/LIVE_ACCEL_COUNTS_PER_G - maze_neutral_right;
                float down = (float)raw[0]/LIVE_ACCEL_COUNTS_PER_G - maze_neutral_down;
                float alpha = elapsed/(50.0f + elapsed);
                if (!maze_filter_valid) { maze_filtered_right = right; maze_filtered_down = down; maze_filter_valid = true; }
                else {
                    maze_filtered_right += alpha*(right-maze_filtered_right);
                    maze_filtered_down += alpha*(down-maze_filtered_down);
                }
                maze_step(&maze_game, maze_filtered_right, maze_filtered_down, elapsed/1000.0f);
                maze_elapsed_ms += elapsed;
                if (maze_game.won) { maze_phase = MAZE_FINISHED; sound_play(SOUND_COMPLETE); show_maze(); return; }
            }
        }
    }
    if ((int32_t)(now-maze_next_display_ms) >= 0) {
        maze_next_display_ms = now + 50u;
        show_maze();
    }
}

static void maze_handle_button(unsigned button) {
    if (maze_phase == MAZE_PLAYING) {
        if (button == 1) { maze_phase = MAZE_PAUSED; maze_menu_selection = 0; maze_game.vx = maze_game.vy = 0; }
    } else if (maze_phase == MAZE_PAUSED) {
        if (button == 0) maze_menu_selection = (maze_menu_selection + 4u) % 5u;
        else if (button == 2) maze_menu_selection = (maze_menu_selection + 1u) % 5u;
        else if (maze_menu_selection == 0) maze_resume();
        else if (maze_menu_selection == 1) { maze_restart(&maze_game); maze_elapsed_ms = 0; maze_begin_centering(); }
        else if (maze_menu_selection == 2) maze_new_game();
        else if (maze_menu_selection == 3) maze_begin_centering();
        else { app_return_home(); }
    } else if (maze_phase == MAZE_CENTERING) {
        if (button == 2) { maze_phase = MAZE_READY; maze_center_samples = 0; }
    } else if (button == 2) { app_return_home(); }
    else if (button == 1) {
        if (maze_phase == MAZE_FINISHED) maze_new_game();
        else maze_begin_centering();
    }
}

static bool maze_can_idle(void) { return maze_phase != MAZE_PLAYING && maze_phase != MAZE_CENTERING; }
const App maze_app = {.can_idle=maze_can_idle,.enter=maze_new_game,.draw=show_maze,.button=maze_handle_button,
    .tick=service_maze,.owns_imu=true,.fast_tick=true};
