// Never returns, not even when asked to exit (APP_EVENT_EXIT): the firmware has to restart
#include <app_api.h>

APP_INFO("Stuck", 0);

int app_main(const app_api_t *api) {
  api->log("stuck: running");
  app_event_t ev;
  for (;;) {
    if (api->input_poll(&ev, 100) && ev.type == APP_EVENT_EXIT) api->log("stuck: ignoring the exit request");
  }
}
