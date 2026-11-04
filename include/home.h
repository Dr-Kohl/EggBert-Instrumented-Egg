#pragma once

#include <stdbool.h>
#include <stddef.h>

#define HOME_VISIBLE_ROWS 7u
// Scroll when selection reaches the third row from the corresponding edge.
#define HOME_SCROLL_TRIGGER 2u

typedef struct { size_t selected, first; } HomeMenu;
void home_menu_move(HomeMenu *menu, size_t count, bool down);
void home_menu_select(HomeMenu *menu, size_t count, size_t index);
extern HomeMenu home_menu;
void home_draw(void);
void home_button(unsigned button);
