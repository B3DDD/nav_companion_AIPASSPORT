#include "nav_protocol.h"

#include <string.h>

static int parse_i32(const char *s, const char *end, int32_t *out)
{
    if (s >= end) return -1;

    int neg = 0;
    if (*s == '-') {
        neg = 1;
        s++;
        if (s >= end) return -1;
    }
    if (s >= end || *s < '0' || *s > '9') return -1;

    int64_t v = 0;
    while (s < end && *s >= '0' && *s <= '9') {
        v = v * 10 + (int64_t)(*s - '0');
        if (v > INT32_MAX) return -1;
        s++;
    }
    if (s != end) return -1;    // 数字字段内出现非数字字符

    *out = (int32_t)(neg ? -v : v);
    return 0;
}

static void copy_field(char *dst, size_t cap, const char *s, size_t n)
{
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = '\0';
}

// 从 *p 读取下一个 '|' 分隔字段; 成功后 *p 停在分隔符(或 end), 字段区间为 [*f, *f+*n)。
// 期望 *p 指向 '|'(字段分隔符), 否则返回 -1。
static int next_field(const char **p, const char *end, const char **f, size_t *n)
{
    if (*p >= end) return -1;
    if (**p != '|') return -1;
    (*p)++;
    *f = *p;
    while (*p < end && **p != '|') (*p)++;
    *n = (size_t)(*p - *f);
    return 0;
}

void nav_update_clear(nav_update_t *u)
{
    u->fix = 0;
    u->state = 0;
    u->wp[0] = '\0';
    u->dist_m = -1;
    u->off_m = -1;
    u->remain_m = -1;
    u->alt_m = -1;
    u->cum_m = -1;
    u->asc_m = -1;
    u->total_m = -1;
    u->prog_pm = -1;
    u->cps_count = 0;
    u->turn[0] = '\0';
}

// 解析逗号分隔的 CP 千分比列表，如 "120,350,610"。
static void parse_cps(const char *s, const char *end, nav_update_t *u)
{
    u->cps_count = 0;
    while (s < end && u->cps_count < NAV_CPS_MAX) {
        while (s < end && (*s == ',' || *s == ' ')) s++;
        if (s >= end) break;
        int32_t v = 0;
        int neg = 0;
        int ok = 0;
        if (*s == '-') {
            neg = 1;
            s++;
        }
        while (s < end && *s >= '0' && *s <= '9') {
            v = v * 10 + (*s - '0');
            s++;
            ok = 1;
        }
        if (ok) u->cps_pm[u->cps_count++] = neg ? -v : v;
        while (s < end && *s != ',') s++;
    }
}

int nav_update_parse(nav_update_t *u, const char *line, size_t len)
{
    nav_update_clear(u);
    const char *p = line;
    const char *end = line + len;

    if (end - p < 3 || memcmp(p, "NAV", 3) != 0) return -1;
    p += 3;

    const char *f;
    size_t n;

    int32_t fix = 0;
    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &fix) != 0) return -1;
    u->fix = (fix == 0) ? 0 : 1;

    int32_t state = 0;
    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &state) != 0) return -1;
    u->state = (state < 0) ? 0 : (state > 2 ? 2 : (int)state);

    if (next_field(&p, end, &f, &n) != 0) return -1;
    copy_field(u->wp, sizeof(u->wp), f, n);

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->dist_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->off_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->remain_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->alt_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->cum_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->asc_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->total_m) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    if (parse_i32(f, f + n, &u->prog_pm) != 0) return -1;

    if (next_field(&p, end, &f, &n) != 0) return -1;
    parse_cps(f, f + n, u);

    if (next_field(&p, end, &f, &n) != 0) return -1;
    copy_field(u->turn, sizeof(u->turn), f, n);

    if (p != end) return -1;    // 多余字段
    return 0;
}

void nav_rx_init(nav_rx_t *rx)
{
    rx->len = 0;
    rx->overflow = false;
}

nav_rx_result_t nav_rx_feed(nav_rx_t *rx, const uint8_t *data, size_t data_len,
                            nav_update_t *out)
{
    nav_rx_result_t result = NAV_RX_NEED_MORE;
    for (size_t i = 0; i < data_len; i++) {
        const uint8_t b = data[i];
        if (b == '\n') {
            if (!rx->overflow && rx->len > 0 &&
                nav_update_parse(out, (const char *)rx->data, rx->len) == 0) {
                result = NAV_RX_GOT_FRAME;
            }
            rx->len = 0;
            rx->overflow = false;
            continue;
        }
        if (rx->overflow) continue;
        if (rx->len >= NAV_RX_BUF_MAX) {
            rx->overflow = true;
            continue;
        }
        rx->data[rx->len++] = b;
    }
    return result;
}
