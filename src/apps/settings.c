#include <stdio.h>
#include "app.h"
#include "settings.h"
#include "ui.h"
#include "firmware_version.h"

static unsigned settings_selection;
static const char *settings_feedback;

static void settings_enter(void) {
    settings_selection=0;
    settings_feedback=NULL;
}

static void show_settings(void) {
    if (!ui_display_available()) return;
    const UserSettings *values=settings_current();
    static const char *lights[]={"LIGHT LOW","LIGHT MID","LIGHT HIGH"};
    ui_begin("SETTINGS");
    ui_menu_item(32,values->sound ? "SOUND ON" : "SOUND OFF",settings_selection==0);
    ui_menu_item(47,lights[values->light],settings_selection==1);
    ui_menu_item(62,"SAVE EXIT",settings_selection==2);
    ssd1306_ui_text(2,82,"FW " EGGBERT_FIRMWARE_VERSION,true);
    if (settings_feedback) ssd1306_ui_text(2,92,settings_feedback,true);
    ssd1306_ui_text(2,104,"UP DN MOVE",true);
    ssd1306_ui_text(2,120,settings_selection==2 ? "MID SAVE" : "MID CHANGE",true);
    ssd1306_show();
}

static void settings_button(unsigned button) {
    if (button==0) settings_selection=(settings_selection+2u)%3u;
    else if (button==2) settings_selection=(settings_selection+1u)%3u;
    else if (button==1) {
        settings_feedback=NULL;
        if (settings_selection==0) settings_set_sound(!settings_current()->sound);
        else if (settings_selection==1) {
            SettingsLight next=(SettingsLight)((settings_current()->light+1u)%SETTINGS_LIGHT_COUNT);
            if (!settings_set_light(next)) settings_feedback="SCREEN ERR";
        } else {
            if (settings_save()) app_return_home();
            else settings_feedback="SAVE ERROR";
        }
    }
}

static bool settings_can_idle(void) { return true; }
const App settings_app={.can_idle=settings_can_idle,.enter=settings_enter,.draw=show_settings,.button=settings_button};
