#include "home.h"

void home_menu_select(HomeMenu *menu, size_t count, size_t index) {
    if (!menu) return;
    if (!count) { menu->selected = menu->first = 0; return; }
    menu->selected = index < count ? index : count - 1;
    size_t last_first = count > HOME_VISIBLE_ROWS ? count - HOME_VISIBLE_ROWS : 0;
    if (menu->first > last_first) menu->first = last_first;
    if (menu->selected < menu->first) menu->first = menu->selected;
    if (menu->selected >= menu->first + HOME_VISIBLE_ROWS)
        menu->first = menu->selected - HOME_VISIBLE_ROWS + 1;
}

void home_menu_move(HomeMenu *menu, size_t count, bool down) {
    if (!menu) return;
    home_menu_select(menu, count, menu->selected);
    if (count <= 1) return;
    if (down && menu->selected == count - 1) {
        menu->selected = menu->first = 0; return;
    }
    if (!down && menu->selected == 0) {
        home_menu_select(menu, count, count - 1); return;
    }
    menu->selected = down ? menu->selected + 1 : menu->selected - 1;
    if (count <= HOME_VISIBLE_ROWS) return;
    size_t last_first = count - HOME_VISIBLE_ROWS;
    if (down && menu->first < last_first &&
        menu->selected - menu->first >= HOME_VISIBLE_ROWS - 1 - HOME_SCROLL_TRIGGER)
        ++menu->first;
    else if (!down && menu->first && menu->selected - menu->first <= HOME_SCROLL_TRIGGER)
        --menu->first;
}
