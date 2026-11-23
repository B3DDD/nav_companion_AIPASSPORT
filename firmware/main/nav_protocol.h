// main/nav_protocol.h —— 导航副屏 BLE 上行协议解析(纯逻辑,与 ESP-IDF/LVGL 解耦)。
//
// 手机(BLE central)通过 NAV 特征值写入以 '\n' 结尾的 UTF-8 帧:
//
//   NAV|<fix>|<state>|<wp_name>|<dist_m>|<off_m>|<remain_m>|<alt_m>|<cum_m>|<asc_m>|<total_m>|<prog_pm>|<cps>|<turn>\n
//
//   fix      0 = 无定位, 1 = 已定位
//   state    0 = 待出发(未进入起点范围), 1 = 导航中, 2 = 已完成
//   wp_name  下一 CP / 航点名称(UTF-8, 不含 '|' 或 '\n')
//   dist_m   到前方参考点距离(整数米, 负数 = 未知)
//   off_m    偏离当前轨迹距离(整数米, 负数 = 未计算)
//   remain_m 剩余里程(整数米, 负数 = 未知)。state=0 时表示"距起点距离"
//   alt_m    当前海拔(整数米, 负数 = 未知)
//   cum_m    累计距离(整数米, 负数 = 未知)
//   asc_m    累计爬升(整数米, 负数 = 未知)
//   total_m  路线总长(整数米, 负数 = 未知), 用于"累计+剩余≈总长"的交叉校验
//   prog_pm  路线进度(千分比 0..1000, 负数 = 未知)
//   cps      CP 刻度: 逗号分隔的千分比, 如 "120,350,610,900"(可空, 最多 8 个)
//   turn     转向提示(UTF-8, 可空)
//
// 首字段是消息类型, 保留以便以后扩展(如 BAT/PING); 本版本只实现 NAV。
// 解析器把字符串字段当字节流原样拷贝, 不做 UTF-8 校验; 长名称可能被按字节截断。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define NAV_WP_NAME_MAX 48
#define NAV_TURN_MAX    32
#define NAV_RX_BUF_MAX  224
#define NAV_CPS_MAX     8

typedef struct {
    int fix;
    int state;
    char wp[NAV_WP_NAME_MAX + 1];
    int32_t dist_m;
    int32_t off_m;
    int32_t remain_m;
    int32_t alt_m;
    int32_t cum_m;
    int32_t asc_m;
    int32_t total_m;
    int32_t prog_pm;
    int cps_count;
    int32_t cps_pm[NAV_CPS_MAX];
    char turn[NAV_TURN_MAX + 1];
} nav_update_t;

typedef enum {
    NAV_RX_NEED_MORE = 0,
    NAV_RX_GOT_FRAME = 1,
} nav_rx_result_t;

typedef struct {
    uint8_t data[NAV_RX_BUF_MAX];
    size_t len;
    bool overflow;      // 当前帧超长, 正在丢弃直到下一个 '\n'
} nav_rx_t;

void nav_update_clear(nav_update_t *u);

// 解析一行(不含 '\n', 以 len 为准, 不要求 NUL 结尾)。成功返回 0, 否则 -1。
int nav_update_parse(nav_update_t *u, const char *line, size_t len);

void nav_rx_init(nav_rx_t *rx);

// 把接收到的字节流喂入缓冲; 每收到完整一行(遇 '\n')即解析到 out。
// 若本段数据含多帧, out 保留最后一帧。返回 NAV_RX_GOT_FRAME 表示 out 有效。
// 超长帧会被丢弃(直到下一个 '\n'), 不影响后续帧。
nav_rx_result_t nav_rx_feed(nav_rx_t *rx, const uint8_t *data, size_t data_len,
                            nav_update_t *out);
