#include "nav_app.h"

void nav_app_init(nav_app_t *app)
{
    nav_state_init(&app->state);
    app->state_lock = xSemaphoreCreateMutex();
    app->events = xEventGroupCreate();
}
