#include "maze.h"

#include <math.h>
#include <string.h>

static const int dx[4] = {0, 1, 0, -1};
static const int dy[4] = {-1, 0, 1, 0};
static const uint8_t wall[4] = {MAZE_NORTH, MAZE_EAST, MAZE_SOUTH, MAZE_WEST};

static uint32_t random_next(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *state = x;
}

static int neighbor(unsigned cell, unsigned direction) {
    int x = (int)(cell % MAZE_COLS) + dx[direction];
    int y = (int)(cell / MAZE_COLS) + dy[direction];
    return x < 0 || y < 0 || x >= (int)MAZE_COLS || y >= (int)MAZE_ROWS ?
        -1 : y * (int)MAZE_COLS + x;
}

static void carve(maze_t *maze, uint32_t *random) {
    uint16_t stack[MAZE_CELLS];
    unsigned depth = 1;
    memset(maze->walls, 15, sizeof maze->walls);
    stack[0] = 0;
    maze->walls[0] |= 16; // Temporary visited bit; removed before returning.
    while (depth) {
        unsigned cell = stack[depth - 1], choices[4], count = 0;
        for (unsigned d = 0; d < 4; ++d) {
            int next = neighbor(cell, d);
            if (next >= 0 && !(maze->walls[next] & 16)) choices[count++] = d;
        }
        if (!count) { --depth; continue; }
        unsigned d = choices[random_next(random) % count];
        unsigned next = (unsigned)neighbor(cell, d);
        maze->walls[cell] &= (uint8_t)~wall[d];
        maze->walls[next] &= (uint8_t)~wall[(d + 2) % 4];
        maze->walls[next] |= 16;
        stack[depth++] = (uint16_t)next;
    }
    for (unsigned i = 0; i < MAZE_CELLS; ++i) maze->walls[i] &= 15;
}

static unsigned score(maze_t *maze) {
    // A path has at most 215 edges. Byte-sized BFS scratch leaves room on the
    // firmware's small stack for the caller and timer/USB interrupts.
    _Static_assert(MAZE_CELLS < UINT8_MAX, "Widen maze scoring scratch for larger grids");
    uint8_t distance[MAZE_CELLS], turns[MAZE_CELLS], queue[MAZE_CELLS], incoming[MAZE_CELLS];
    memset(distance, 0xff, sizeof distance);
    distance[0] = turns[0] = incoming[0] = 0;
    unsigned head = 0, tail = 1, farthest = 0;
    queue[0] = 0;
    maze->dead_ends = 0;
    while (head < tail) {
        unsigned cell = queue[head++], exits = 0;
        for (unsigned d = 0; d < 4; ++d) {
            if (maze->walls[cell] & wall[d]) continue;
            ++exits;
            int next = neighbor(cell, d);
            if (next < 0 || distance[next] != UINT8_MAX) continue;
            distance[next] = (uint8_t)(distance[cell] + 1);
            incoming[next] = (uint8_t)d;
            turns[next] = (uint8_t)(turns[cell] + (cell != 0 && incoming[cell] != d));
            queue[tail++] = (uint8_t)next;
            if (distance[next] > distance[farthest]) farthest = (unsigned)next;
        }
        if (exits == 1) ++maze->dead_ends;
    }
    maze->goal = (uint16_t)farthest;
    maze->solution_steps = distance[farthest];
    maze->solution_turns = turns[farthest];
    return maze->solution_steps + 2u * maze->solution_turns + 3u * maze->dead_ends;
}

void maze_generate(maze_t *maze, uint32_t seed) {
    maze_t candidate = {0};
    uint32_t random = seed ? seed : 0x6d617a65u;
    unsigned best = 0;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        carve(&candidate, &random);
        unsigned quality = score(&candidate);
        if (quality > best) { *maze = candidate; best = quality; }
    }
    maze->seed = seed;
    maze_restart(maze);
}

void maze_restart(maze_t *maze) {
    maze->x = maze->y = MAZE_CELL_PIXELS * 0.5f;
    maze->vx = maze->vy = 0;
    maze->camera_x = maze->camera_y = 0;
    maze->won = false;
}

bool maze_wall_pixel(const maze_t *maze, int x, int y) {
    if (x <= 0 || y <= 0 || x >= (int)MAZE_WORLD_WIDTH - 1 ||
        y >= (int)MAZE_WORLD_HEIGHT - 1) return true;
    unsigned col = (unsigned)x / MAZE_CELL_PIXELS;
    unsigned row = (unsigned)y / MAZE_CELL_PIXELS;
    if (x % MAZE_CELL_PIXELS == 0 && y % MAZE_CELL_PIXELS == 0) return true;
    if (x % MAZE_CELL_PIXELS == 0) return (maze->walls[row * MAZE_COLS + col] & MAZE_WEST) != 0;
    if (y % MAZE_CELL_PIXELS == 0) return (maze->walls[row * MAZE_COLS + col] & MAZE_NORTH) != 0;
    return false;
}

static float clampf(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

bool maze_can_place(const maze_t *maze, float x, float y) {
    if (!isfinite(x) || !isfinite(y) || x < MAZE_BALL_RADIUS + 0.5f ||
        y < MAZE_BALL_RADIUS + 0.5f || x > MAZE_WORLD_WIDTH - 1.5f - MAZE_BALL_RADIUS ||
        y > MAZE_WORLD_HEIGHT - 1.5f - MAZE_BALL_RADIUS) return false;
    int left = (int)floorf(x - MAZE_BALL_RADIUS - 0.5f);
    int top = (int)floorf(y - MAZE_BALL_RADIUS - 0.5f);
    for (int py = top; py <= (int)ceilf(y + MAZE_BALL_RADIUS + 0.5f); ++py) {
        for (int px = left; px <= (int)ceilf(x + MAZE_BALL_RADIUS + 0.5f); ++px) {
            if (!maze_wall_pixel(maze, px, py)) continue;
            // Wall pixels occupy unit squares. Test the circle against each
            // square, including shared wall ends so diagonal corners stay solid.
            float ex = x - clampf(x, px - 0.5f, px + 0.5f);
            float ey = y - clampf(y, py - 0.5f, py + 0.5f);
            if (ex * ex + ey * ey < MAZE_BALL_RADIUS * MAZE_BALL_RADIUS) return false;
        }
    }
    return true;
}

void maze_scroll(maze_t *maze) {
    int x = (int)floorf(maze->x), y = (int)floorf(maze->y);
    if (x - maze->camera_x < MAZE_SCROLL_MARGIN) maze->camera_x = x - MAZE_SCROLL_MARGIN;
    if (x - maze->camera_x > MAZE_VIEW_WIDTH - 1 - MAZE_SCROLL_MARGIN)
        maze->camera_x = x - (MAZE_VIEW_WIDTH - 1 - MAZE_SCROLL_MARGIN);
    if (y - maze->camera_y < MAZE_SCROLL_MARGIN) maze->camera_y = y - MAZE_SCROLL_MARGIN;
    if (y - maze->camera_y > MAZE_VIEW_HEIGHT - 1 - MAZE_SCROLL_MARGIN)
        maze->camera_y = y - (MAZE_VIEW_HEIGHT - 1 - MAZE_SCROLL_MARGIN);
    int max_x = (int)MAZE_WORLD_WIDTH - MAZE_VIEW_WIDTH;
    int max_y = (int)MAZE_WORLD_HEIGHT - MAZE_VIEW_HEIGHT;
    if (maze->camera_x < 0) maze->camera_x = 0;
    if (maze->camera_y < 0) maze->camera_y = 0;
    if (maze->camera_x > max_x) maze->camera_x = max_x;
    if (maze->camera_y > max_y) maze->camera_y = max_y;
}

static float input_tilt(float g) {
    // About one degree of dead zone. Saturate steering near 20 degrees.
    float magnitude = fabsf(g);
    if (magnitude <= 0.02f) return 0;
    return copysignf(clampf(magnitude - 0.02f, 0, 0.33f), g);
}

void maze_step(maze_t *maze, float right_g, float down_g, float seconds) {
    if (maze->won || !isfinite(seconds) || seconds <= 0 ||
        !isfinite(right_g) || !isfinite(down_g)) return;
    seconds = clampf(seconds, 0, 0.1f);
    unsigned steps = (unsigned)ceilf(seconds / 0.005f);
    float dt = seconds / steps;
    float ax = 420.0f * input_tilt(right_g), ay = 420.0f * input_tilt(down_g);
    for (unsigned i = 0; i < steps; ++i) {
        // Implicit drag is stable even after display/USB scheduling delays.
        maze->vx = clampf((maze->vx + ax * dt) / (1 + 3.0f * dt), -48, 48);
        maze->vy = clampf((maze->vy + ay * dt) / (1 + 3.0f * dt), -48, 48);
        float x = maze->x + maze->vx * dt, y = maze->y + maze->vy * dt;
        if (maze_can_place(maze, x, maze->y)) maze->x = x; else maze->vx = 0;
        if (maze_can_place(maze, maze->x, y)) maze->y = y; else maze->vy = 0;
        float gx = (maze->goal % MAZE_COLS + 0.5f) * MAZE_CELL_PIXELS;
        float gy = (maze->goal / MAZE_COLS + 0.5f) * MAZE_CELL_PIXELS;
        if (fabsf(maze->x - gx) < 2.5f && fabsf(maze->y - gy) < 2.5f) {
            maze->won = true; maze->vx = maze->vy = 0; break;
        }
    }
    maze_scroll(maze);
}

unsigned maze_goal_direction(const maze_t *maze) {
    float x = (maze->goal % MAZE_COLS + 0.5f) * MAZE_CELL_PIXELS - maze->x;
    float y = (maze->goal / MAZE_COLS + 0.5f) * MAZE_CELL_PIXELS - maze->y;
    if (fabsf(x) < fabsf(y) * 0.41421356f) return y < 0 ? 0 : 4;
    if (fabsf(y) < fabsf(x) * 0.41421356f) return x > 0 ? 2 : 6;
    return y < 0 ? (x > 0 ? 1 : 7) : (x > 0 ? 3 : 5);
}
