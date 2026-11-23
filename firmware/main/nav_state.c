#include "nav_state.h"

void nav_state_init(nav_state_t *s)
{
    nav_update_clear(&s->update);
    s->off_level = NAV_OFF_OK;
    s->seq = 0;
    s->has_data = false;
}

bool nav_state_apply(nav_state_t *s, const nav_update_t *u)
{
    const nav_off_level_t level = nav_off_level(u->off_m);
    const bool entered_alert = (level == NAV_OFF_ALERT) && (s->off_level != NAV_OFF_ALERT);

    s->update = *u;
    s->off_level = level;
    s->has_data = true;
    s->seq++;
    return entered_alert;
}
