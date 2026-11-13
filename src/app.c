#include "app.h"
#include "app_services.h"
#include "home.h"

static const App *current;
static AppSample live_sample;

const App *app_current(void) { return current; }
const AppSample *app_sample_current(void) { return &live_sample; }

bool app_open(size_t index) {
    if (index >= app_entry_count || !app_entries[index].app || !app_host_can_open()) return false;
    if (current && current->leave) current->leave();
    home_menu_select(&home_menu, app_entry_count, index);
    current = app_entries[index].app;
    app_host_prepare();
    if (current->enter) current->enter();
    return true;
}

void app_return_home(void) {
    if (!app_host_can_open()) return;
    const App *previous = current;
    current = NULL;
    if (previous && previous->leave) previous->leave();
    app_host_home();
}

void app_draw(void) { if (current && current->draw) current->draw(); }
void app_button(unsigned button) { if (button < 3 && current && current->button) current->button(button); }
void app_tick(void) { if (current && current->tick) current->tick(); }
void app_publish_sample(const AppSample *sample) {
    if (!sample) return;
    live_sample = *sample;
    if (current && current->sample) current->sample(sample);
}
uint32_t app_sample_period_ms(void) {
    uint32_t period = current && current->sample_period_ms ? current->sample_period_ms() : 250u;
    return period ? period : 1u;
}
bool app_can_idle(void) {
    return !current || (current->can_idle && current->can_idle());
}
