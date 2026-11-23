#include <assert.h>
#include <string.h>
#include "nav_format.h"

static void test_meters(void)
{
    char b[32];

    nav_format_meters(b, sizeof(b), 0);
    assert(strcmp(b, "0m") == 0);
    nav_format_meters(b, sizeof(b), 320);
    assert(strcmp(b, "320m") == 0);
    nav_format_meters(b, sizeof(b), 999);
    assert(strcmp(b, "999m") == 0);
    nav_format_meters(b, sizeof(b), 1000);
    assert(strcmp(b, "1.00km") == 0);
    nav_format_meters(b, sizeof(b), 1250);
    assert(strcmp(b, "1.25km") == 0);
    nav_format_meters(b, sizeof(b), 68000);
    assert(strcmp(b, "68.00km") == 0);
    nav_format_meters(b, sizeof(b), -1);
    assert(strcmp(b, "--") == 0);
}

static void test_off_level(void)
{
    assert(nav_off_level(-1) == NAV_OFF_OK);
    assert(nav_off_level(0) == NAV_OFF_OK);
    assert(nav_off_level(29) == NAV_OFF_OK);
    assert(nav_off_level(30) == NAV_OFF_WARN);
    assert(nav_off_level(99) == NAV_OFF_WARN);
    assert(nav_off_level(100) == NAV_OFF_ALERT);
    assert(nav_off_level(5000) == NAV_OFF_ALERT);
}

int main(void)
{
    test_meters();
    test_off_level();
    return 0;
}
