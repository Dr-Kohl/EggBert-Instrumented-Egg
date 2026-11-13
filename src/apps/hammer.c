#include "app.h"
#include "app_services.h"

// Hammer Hit shares the protected recording buffers and sensor scheduler with
// Record and Beam. Active acquisition is serviced even while app ticks pause.
static void hammer_enter(void) { capture_app_enter(CAPTURE_APP_HAMMER); }
const App hammer_app = {.can_idle=capture_app_can_idle,.enter=hammer_enter,.draw=capture_app_draw,.button=capture_app_button};
