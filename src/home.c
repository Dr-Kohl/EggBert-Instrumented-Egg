#include "app.h"
#include "home.h"
#include "ui.h"

HomeMenu home_menu;

void home_draw(void) {
    if (!ui_display_available()) return;
    ui_begin("HOME");
    size_t count = app_entry_count;
    for (size_t row = 0; row < HOME_VISIBLE_ROWS && home_menu.first + row < count; ++row) {
        size_t index = home_menu.first + row;
        ui_menu_item((uint8_t)(30u + 13u*row), app_entries[index].label, index == home_menu.selected);
    }
    if (count > HOME_VISIBLE_ROWS) {
        // A separate gutter keeps the thumb readable on highlighted rows.
        ssd1306_ui_fill_rect(62, 28, 2, 90, false);
        unsigned height = (unsigned)(90u*HOME_VISIBLE_ROWS/count);
        if (height < 3) height = 3;
        unsigned top = (unsigned)((90u-height)*home_menu.first/(count-HOME_VISIBLE_ROWS));
        ssd1306_ui_fill_rect(63, (uint8_t)(28u+top), 1, (uint8_t)height, true);
    }
    ssd1306_ui_text(2, 120, "MID SELECT", true);
    ssd1306_show();
}

void home_button(unsigned button) {
    if (button == 0 || button == 2) home_menu_move(&home_menu, app_entry_count, button == 2);
    else if (button == 1 && app_entry_count) app_open(home_menu.selected);
}
