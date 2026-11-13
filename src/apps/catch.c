#include "app.h"
#include "app_services.h"
// Acquisition and its protected buffer are shared firmware services. This
// adapter supplies a catalog entry without duplicating the capture pipeline.
static void catch_enter(void) { capture_app_enter(CAPTURE_APP_CATCH); }
const App catch_app = {.can_idle=capture_app_can_idle,.enter=catch_enter,.draw=capture_app_draw,.button=capture_app_button};
