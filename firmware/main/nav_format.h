// main/nav_format.h —— 导航距离格式化与偏离等级(纯逻辑,与 ESP-IDF/LVGL 解耦)。
#pragma once

#include <stdint.h>
#include <stddef.h>

// 米数格式化为 "320m" / "1.25km"; 负数或不可用输出 "--"。
// 返回写入 out 的字节数(不含 NUL); cap 不足时返回本应写入的字节数(snprintf 语义)。
int nav_format_meters(char *out, size_t cap, int32_t meters);

typedef enum {
    NAV_OFF_OK = 0,     // 正常/未偏离
    NAV_OFF_WARN = 1,   // 轻度偏离, 提示
    NAV_OFF_ALERT = 2,  // 明显偏离, 告警(配提示音)
} nav_off_level_t;

// 偏离告警阈值(米)
#define NAV_OFF_WARN_M   30
#define NAV_OFF_ALERT_M 100

// off_m < 0 视为无偏离信息, 返回 NAV_OFF_OK。
nav_off_level_t nav_off_level(int32_t off_m);
