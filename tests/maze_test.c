#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "maze.h"

static const int dx[4] = {0,1,0,-1}, dy[4] = {-1,0,1,0};
static const unsigned walls[4] = {MAZE_NORTH,MAZE_EAST,MAZE_SOUTH,MAZE_WEST};

static void verify_layout(const maze_t *maze) {
    int distance[MAZE_CELLS];
    unsigned queue[MAZE_CELLS], head = 0, tail = 1, edges = 0, farthest = 0;
    for (unsigned i = 0; i < MAZE_CELLS; ++i) distance[i] = -1;
    distance[0] = 0; queue[0] = 0;
    while (head < tail) {
        unsigned cell = queue[head++];
        for (unsigned d = 0; d < 4; ++d) {
            int x = (int)(cell % MAZE_COLS)+dx[d], y = (int)(cell / MAZE_COLS)+dy[d];
            if (x < 0 || y < 0 || x >= (int)MAZE_COLS || y >= (int)MAZE_ROWS) {
                assert(maze->walls[cell] & walls[d]);
                continue;
            }
            unsigned next = (unsigned)y*MAZE_COLS+(unsigned)x;
            assert(!!(maze->walls[cell] & walls[d]) == !!(maze->walls[next] & walls[(d+2)%4]));
            if (maze->walls[cell] & walls[d]) continue;
            ++edges;
            // Every graph passage must also fit the physical ball, including
            // where a passage crosses the cell grid's shared wall position.
            for (unsigned step = 0; step <= MAZE_CELL_PIXELS*4; ++step) {
                float px = (cell%MAZE_COLS+0.5f)*MAZE_CELL_PIXELS + dx[d]*(float)step*0.25f;
                float py = (cell/MAZE_COLS+0.5f)*MAZE_CELL_PIXELS + dy[d]*(float)step*0.25f;
                assert(maze_can_place(maze, px, py));
            }
            if (distance[next] >= 0) continue;
            distance[next] = distance[cell]+1;
            queue[tail++] = next;
            if (distance[next] > distance[farthest]) farthest = next;
        }
    }
    assert(tail == MAZE_CELLS);
    assert(edges == 2*(MAZE_CELLS-1)); // A connected tree: exactly one solution.
    assert(distance[maze->goal] == distance[farthest]);
    assert(distance[maze->goal] == maze->solution_steps);
    assert(maze->solution_steps >= MAZE_CELLS/3);
    assert(maze->solution_turns >= 25 && maze->dead_ends >= 12);
}

static void verify_physics(void) {
    maze_t maze = {0};
    memset(maze.walls, 15, sizeof maze.walls);
    maze.walls[0] &= (uint8_t)~MAZE_EAST;
    maze.walls[1] &= (uint8_t)~MAZE_WEST;
    maze.goal = MAZE_CELLS-1;
    maze_restart(&maze);
    for (unsigned i = 0; i < 100; ++i) maze_step(&maze, 0.019f, -0.019f, 0.01f);
    assert(maze.x == 6 && maze.y == 6); // Dead zone.
    for (unsigned i = 0; i < 200; ++i) {
        maze_step(&maze, 0.3f, -0.3f, 0.02f);
        assert(maze_can_place(&maze, maze.x, maze.y));
    }
    assert(maze.x > 20 && maze.x < 22); // Traverses opening, stops before wall.
    assert(maze.y > 2 && maze.y < 3); // Slides along north wall.
    for (unsigned i = 0; i < 200; ++i) maze_step(&maze, -0.3f, 0.3f, 0.02f);
    assert(maze.x > 2 && maze.x < 3 && maze.y > 9 && maze.y < 10);
    maze_restart(&maze);
    maze_step(&maze, 0.3f, 0, 0.1f);
    float fast = maze.vx;
    maze_step(&maze, 0, 0, 0.1f);
    assert(maze.vx > 0 && maze.vx < fast); // Coasts with moderate drag.
    maze_t delayed = maze;
    maze_step(&maze, 0.3f, 0, 30);
    maze_step(&delayed, 0.3f, 0, 0.1f);
    assert(maze.x == delayed.x && maze.vx == delayed.vx);
    float previous = maze.x;
    maze_step(&maze, NAN, 0, 1);
    assert(maze.x == previous);
    maze.x = 23-MAZE_BALL_RADIUS; maze.y = 6; maze.vx = 1e6f;
    maze_step(&maze, 1, 0, 0.1f);
    assert(maze.x < 22 && maze_can_place(&maze, maze.x, maze.y));
    maze_restart(&maze);
    maze.goal = 1;
    for (unsigned i = 0; i < 200 && !maze.won; ++i) maze_step(&maze, 0.3f, 0, 0.01f);
    assert(maze.won && maze.vx == 0 && maze.vy == 0);
    previous = maze.x;
    maze_step(&maze, -1, -1, 0.1f);
    assert(maze.x == previous);
    uint16_t goal = maze.goal;
    maze_restart(&maze);
    assert(!maze.won && maze.x == 6 && maze.goal == goal);
}

static void verify_camera(void) {
    maze_t maze = {0};
    maze.x = 30; maze.y = 40;
    maze_scroll(&maze);
    assert(maze.camera_x == 0 && maze.camera_y == 0);
    maze.x = 48; maze.y = 72;
    maze_scroll(&maze);
    assert(maze.camera_x == 1 && maze.camera_y == 1);
    maze.x = 60; maze.y = 90;
    maze_scroll(&maze);
    assert(maze.camera_x == 13 && maze.camera_y == 19);
    maze.x = 50; maze.y = 80;
    maze_scroll(&maze);
    assert(maze.camera_x == 13 && maze.camera_y == 19); // No center-lock.
    maze.x = 28; maze.y = 34;
    maze_scroll(&maze);
    assert(maze.camera_x == 12 && maze.camera_y == 18); // Pushes back at margin.
    maze.x = MAZE_WORLD_WIDTH-6; maze.y = MAZE_WORLD_HEIGHT-6;
    maze_scroll(&maze);
    assert(maze.camera_x == (int)MAZE_WORLD_WIDTH-MAZE_VIEW_WIDTH);
    assert(maze.camera_y == (int)MAZE_WORLD_HEIGHT-MAZE_VIEW_HEIGHT);
    maze.x = maze.y = 6; maze_scroll(&maze);
    assert(maze.camera_x == 0 && maze.camera_y == 0);
    maze.x = maze.y = 6;
    maze.goal = 1; assert(maze_goal_direction(&maze) == 2);
    maze.goal = MAZE_COLS; assert(maze_goal_direction(&maze) == 4);
}

int main(void) {
    unsigned min_route = MAZE_CELLS, min_turns = MAZE_CELLS, min_dead = MAZE_CELLS;
    uint32_t random = 12345;
    for (unsigned seed = 0; seed < 1000; ++seed) {
        maze_t maze, replay;
        maze_generate(&maze, seed); maze_generate(&replay, seed);
        assert(!memcmp(maze.walls, replay.walls, sizeof maze.walls));
        assert(maze.goal == replay.goal && maze.seed == seed);
        verify_layout(&maze);
        if (maze.solution_steps < min_route) min_route = maze.solution_steps;
        if (maze.solution_turns < min_turns) min_turns = maze.solution_turns;
        if (maze.dead_ends < min_dead) min_dead = maze.dead_ends;
        if (seed < 40) for (unsigned i = 0; i < 2000; ++i) {
            random = random*1664525u+1013904223u;
            float right = ((int)(random%1001)-500)/1000.0f;
            random = random*1664525u+1013904223u;
            float down = ((int)(random%1001)-500)/1000.0f;
            maze_step(&maze, right, down, 0.05f);
            assert(maze_can_place(&maze, maze.x, maze.y));
        }
    }
    verify_physics(); verify_camera();
    printf("Maze tests passed: 1000 seeds, every passage fits ball, 80000 motion steps.\n");
    printf("Minimum route %u cells, %u turns, %u dead ends.\n", min_route, min_turns, min_dead);
    return 0;
}
