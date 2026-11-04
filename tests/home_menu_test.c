#include <assert.h>
#include <stdio.h>
#include "home.h"

int main(void) {
    HomeMenu menu={0};
    for (unsigned i=0;i<3;++i) home_menu_move(&menu,11,true);
    assert(menu.selected==3 && menu.first==0);
    home_menu_move(&menu,11,true);
    assert(menu.selected==4 && menu.first==1); // Third from bottom pushes view.
    home_menu_move(&menu,11,true);
    assert(menu.selected==5 && menu.first==2);
    home_menu_move(&menu,11,false);
    assert(menu.selected==4 && menu.first==1); // Third from top pushes back.
    for (unsigned i=0;i<6;++i) home_menu_move(&menu,11,true);
    assert(menu.selected==10 && menu.first==4);
    home_menu_move(&menu,11,true); assert(menu.selected==0 && menu.first==0);
    home_menu_move(&menu,11,false); assert(menu.selected==10 && menu.first==4);
    // Empty, short, exact-fit, and arbitrarily long catalogs; selection always
    // stays visible and wrapping preserves the first/last item.
    for (size_t count=0;count<100;++count) {
        menu=(HomeMenu){0};
        for (size_t i=0;i<1000;++i) {
            home_menu_move(&menu,count,(i%13)<7);
            if (!count) { assert(menu.selected==0 && menu.first==0); continue; }
            assert(menu.selected<count && menu.first<=menu.selected);
            assert(menu.selected<menu.first+HOME_VISIBLE_ROWS);
            assert(menu.first <= (count>HOME_VISIBLE_ROWS ? count-HOME_VISIBLE_ROWS : 0));
        }
        home_menu_select(&menu,count,count+10);
        assert(menu.selected==(count ? count-1 : 0));
    }
    puts("Home scrolling: thresholds, wrap, short/empty catalogs and 100000 moves passed.");
    return 0;
}
