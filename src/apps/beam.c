#include "app.h"
#include "app_services.h"
// Acquisition and its protected buffer are shared firmware services. This
// adapter supplies a catalog entry without duplicating the capture pipeline.
static void beam_enter(void) { capture_app_enter(CAPTURE_APP_BEAM); }
static void beam_sample(const AppSample *sample) { (void)sample; capture_app_refresh_live(); }
const App beam_app = {.can_idle=capture_app_can_idle,.enter=beam_enter,.draw=capture_app_draw,.button=capture_app_button,.sample=beam_sample,.sample_period_ms=capture_app_sample_period_ms};
