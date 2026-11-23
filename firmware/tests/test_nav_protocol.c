#include <assert.h>
#include <string.h>
#include "nav_protocol.h"

// 帧格式（v0.3）:
// NAV|fix|state|wp|dist_m|off_m|remain_m|alt_m|cum_m|asc_m|total_m|prog_pm|cps|turn\n
// 共 13 个字段（cps / turn 可为空，需保留各自的 '|'）

static void test_parse_ok(void)
{
    nav_update_t u;
    const char *line = "NAV|1|1|\xe9\x87\x91\xe5\xb1\xb1\xe5\x9e\xae\xe5\x8f\xa3|1250|32|6800|1234|3250|450|10050|420|120,350,610|\xe5\xb7\xa6\xe8\xbd\xac";
    assert(nav_update_parse(&u, line, strlen(line)) == 0);
    assert(u.fix == 1);
    assert(u.state == 1);
    assert(strcmp(u.wp, "\xe9\x87\x91\xe5\xb1\xb1\xe5\x9e\xae\xe5\x8f\xa3") == 0);
    assert(u.dist_m == 1250);
    assert(u.off_m == 32);
    assert(u.remain_m == 6800);
    assert(u.alt_m == 1234);
    assert(u.cum_m == 3250);
    assert(u.asc_m == 450);
    assert(u.total_m == 10050);
    assert(u.prog_pm == 420);
    assert(u.cps_count == 3);
    assert(u.cps_pm[0] == 120);
    assert(u.cps_pm[1] == 350);
    assert(u.cps_pm[2] == 610);
    assert(strcmp(u.turn, "\xe5\xb7\xa6\xe8\xbd\xac") == 0);
}

static void test_parse_no_cps(void)
{
    nav_update_t u;
    const char *line = "NAV|1|1|a|1|2|3|4|5|6|7|0||";
    assert(nav_update_parse(&u, line, strlen(line)) == 0);
    assert(u.cps_count == 0);
    assert(u.prog_pm == 0);
}

static void test_parse_unknown_negative(void)
{
    nav_update_t u;
    const char *line = "NAV|0|0||-1|-1|-1|-1|-1|-1|-1|-1||";
    assert(nav_update_parse(&u, line, strlen(line)) == 0);
    assert(u.fix == 0);
    assert(u.state == 0);
    assert(strcmp(u.wp, "") == 0);
    assert(u.dist_m == -1);
    assert(u.prog_pm == -1);
    assert(u.cps_count == 0);
    assert(strcmp(u.turn, "") == 0);
}

static void test_parse_pending_state(void)
{
    // 待出发：state=0 时 remain_m 表示"距起点距离"
    nav_update_t u;
    const char *line = "NAV|0|0||-1|-1|320|-1|-1|-1|10050|-1|120,350|";
    assert(nav_update_parse(&u, line, strlen(line)) == 0);
    assert(u.state == 0);
    assert(u.remain_m == 320);
    assert(u.total_m == 10050);
    assert(u.cps_count == 2);
}

static void test_parse_fix_and_state_clamp(void)
{
    nav_update_t u;
    const char *line = "NAV|2|9|a|1|2|3|4|5|6|7|800||";
    assert(nav_update_parse(&u, line, strlen(line)) == 0);
    assert(u.fix == 1);
    assert(u.state == 2);

    const char *neg = "NAV|1|-3|a|1|2|3|4|5|6|7|800||";
    assert(nav_update_parse(&u, neg, strlen(neg)) == 0);
    assert(u.state == 0);
}

static void test_parse_bad_type(void)
{
    nav_update_t u;
    const char *line = "XXX|1|1|a|1|2|3|4|5|6|7|800||";
    assert(nav_update_parse(&u, line, strlen(line)) == -1);
}

static void test_parse_bad_numeric(void)
{
    nav_update_t u;
    const char *line = "NAV|1|1|a|abc|2|3|4|5|6|7|800||";
    assert(nav_update_parse(&u, line, strlen(line)) == -1);
}

static void test_parse_too_many_fields(void)
{
    nav_update_t u;
    const char *line = "NAV|1|1|a|1|2|3|4|5|6|7|800||x|y";
    assert(nav_update_parse(&u, line, strlen(line)) == -1);
}

static void test_parse_too_few_fields(void)
{
    nav_update_t u;
    const char *line = "NAV|1|1|a|1|2";
    assert(nav_update_parse(&u, line, strlen(line)) == -1);
}

static void test_cps_overflow_ignored(void)
{
    // 超过 NAV_CPS_MAX 的刻度只保留前 8 个
    nav_update_t u;
    const char *line = "NAV|1|1|a|1|2|3|4|5|6|7|800|1,2,3,4,5,6,7,8,9,10||";
    assert(nav_update_parse(&u, line, strlen(line)) == 0);
    assert(u.cps_count == NAV_CPS_MAX);
    assert(u.cps_pm[0] == 1);
    assert(u.cps_pm[7] == 8);
}

static void test_rx_chunked(void)
{
    nav_rx_t rx;
    nav_update_t u;
    nav_rx_init(&rx);
    const uint8_t part1[] = "NAV|1|1|";
    const uint8_t part2[] = "ok|500|10|3000|12|34|45|67|500|120,350|\n";
    assert(nav_rx_feed(&rx, part1, sizeof(part1) - 1, &u) == NAV_RX_NEED_MORE);
    assert(nav_rx_feed(&rx, part2, sizeof(part2) - 1, &u) == NAV_RX_GOT_FRAME);
    assert(u.state == 1);
    assert(u.dist_m == 500);
    assert(u.cum_m == 34);
    assert(u.cps_count == 2);
    assert(strcmp(u.wp, "ok") == 0);
}

static void test_rx_multi_frame_last_wins(void)
{
    nav_rx_t rx;
    nav_update_t u;
    nav_rx_init(&rx);
    const uint8_t data[] = "NAV|1|1|a|1|2|3|4|5|6|7|800||\nNAV|0|0|b|9|8|7|6|5|4|3|100||\n";
    assert(nav_rx_feed(&rx, data, sizeof(data) - 1, &u) == NAV_RX_GOT_FRAME);
    assert(u.fix == 0);
    assert(u.state == 0);
    assert(u.dist_m == 9);
    assert(strcmp(u.wp, "b") == 0);
}

static void test_rx_overflow_discards_then_recovers(void)
{
    nav_rx_t rx;
    nav_update_t u;
    nav_rx_init(&rx);
    uint8_t big[NAV_RX_BUF_MAX + 10];
    for (size_t i = 0; i < sizeof(big); i++) big[i] = 'x';
    assert(nav_rx_feed(&rx, big, sizeof(big), &u) == NAV_RX_NEED_MORE);
    const uint8_t nl[1] = {'\n'};
    assert(nav_rx_feed(&rx, nl, 1, &u) == NAV_RX_NEED_MORE);  // 超长帧被丢弃
    const uint8_t good[] = "NAV|1|1|ok|1|2|3|4|5|6|7|800||\n";
    assert(nav_rx_feed(&rx, good, sizeof(good) - 1, &u) == NAV_RX_GOT_FRAME);
    assert(strcmp(u.wp, "ok") == 0);
}

int main(void)
{
    test_parse_ok();
    test_parse_no_cps();
    test_parse_unknown_negative();
    test_parse_pending_state();
    test_parse_fix_and_state_clamp();
    test_parse_bad_type();
    test_parse_bad_numeric();
    test_parse_too_many_fields();
    test_parse_too_few_fields();
    test_cps_overflow_ignored();
    test_rx_chunked();
    test_rx_multi_frame_last_wins();
    test_rx_overflow_discards_then_recovers();
    return 0;
}
