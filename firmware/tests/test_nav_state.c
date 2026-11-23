#include <assert.h>
#include "nav_state.h"

static void test_init_and_apply(void)
{
    nav_state_t s;
    nav_state_init(&s);
    assert(s.has_data == false);
    assert(s.seq == 0);
    assert(s.off_level == NAV_OFF_OK);

    nav_update_t u;
    nav_update_clear(&u);
    u.fix = 1;
    u.dist_m = 100;
    u.off_m = 5;
    u.remain_m = 1000;

    assert(nav_state_apply(&s, &u) == false);  // OK, 无告警
    assert(s.has_data == true);
    assert(s.seq == 1);
    assert(s.off_level == NAV_OFF_OK);
    assert(s.update.dist_m == 100);
    assert(s.update.off_m == 5);
}

static void test_alert_rising_edge(void)
{
    nav_state_t s;
    nav_state_init(&s);
    nav_update_t u;
    nav_update_clear(&u);

    u.off_m = 20;    // OK
    assert(nav_state_apply(&s, &u) == false);

    u.off_m = 50;    // WARN
    assert(nav_state_apply(&s, &u) == false);

    u.off_m = 120;   // ALERT 上升沿
    assert(nav_state_apply(&s, &u) == true);

    u.off_m = 130;   // 仍 ALERT, 非上升沿
    assert(nav_state_apply(&s, &u) == false);

    u.off_m = 10;    // 回落到 OK
    assert(nav_state_apply(&s, &u) == false);

    u.off_m = 200;   // 再次 ALERT 上升沿
    assert(nav_state_apply(&s, &u) == true);
}

int main(void)
{
    test_init_and_apply();
    test_alert_rising_edge();
    return 0;
}
