#pragma once

#include <stdbool.h>
#include <stdint.h>

// World dimensions exceed the OLED viewport. Adjacent cells share 1 px walls.
#define MAZE_COLS 12u
#define MAZE_ROWS 18u
#define MAZE_CELLS (MAZE_COLS * MAZE_ROWS)
#define MAZE_CELL_PIXELS 12u
#define MAZE_WORLD_WIDTH (MAZE_COLS * MAZE_CELL_PIXELS + 1u)
#define MAZE_WORLD_HEIGHT (MAZE_ROWS * MAZE_CELL_PIXELS + 1u)
#define MAZE_VIEW_WIDTH 64
#define MAZE_VIEW_HEIGHT 88
#define MAZE_SCROLL_MARGIN 16
#define MAZE_BALL_RADIUS 1.7f

enum { MAZE_NORTH = 1, MAZE_EAST = 2, MAZE_SOUTH = 4, MAZE_WEST = 8 };

typedef struct {
    uint8_t walls[MAZE_CELLS];
    uint16_t goal, solution_steps, solution_turns, dead_ends;
    uint32_t seed;
    float x, y, vx, vy;
    int camera_x, camera_y;
    bool won;
} maze_t;

// Deterministic seed allows replay/debugging. Chooses among eight connected
// randomized spanning trees, favoring long winding routes with branching.
void maze_generate(maze_t *maze, uint32_t seed);
void maze_restart(maze_t *maze);
bool maze_wall_pixel(const maze_t *maze, int x, int y);
bool maze_can_place(const maze_t *maze, float x, float y);
// Inputs are screen-right/down tilt components in g, relative to neutral.
// Integrates rolling motion with drag in <=5 ms substeps; caps a delayed tick
// at 100 ms so a stalled foreground task cannot teleport the ball.
void maze_step(maze_t *maze, float right_g, float down_g, float seconds);
// A stationary camera follows only when the ball pushes into a 16 px margin.
void maze_scroll(maze_t *maze);
// Eight-way direct bearing to the goal, clockwise from north. Not a solution.
unsigned maze_goal_direction(const maze_t *maze);
