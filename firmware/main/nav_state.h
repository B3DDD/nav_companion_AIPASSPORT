// main/nav_state.h —— 导航副屏共享状态(纯逻辑,与 FreeRTOS/ESP-IDF/LVGL 解耦)。
// 锁与事件由上层(nav_app)负责,本模块只描述"一帧数据如何进入状态"。
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "nav_protocol.h"
#include "nav_format.h"

typedef struct {
    nav_update_t update;
    nav_off_level_t off_level;   // 由 update.off_m 推导
    uint32_t seq;                // 每成功应用一帧 +1, UI 据此判断是否有新数据
    bool has_data;               // 是否收到过至少一帧有效数据
} nav_state_t;

void nav_state_init(nav_state_t *s);

// 应用一帧更新并重新推导偏离等级。返回 true 表示偏离等级进入 ALERT(上升沿),
// 调用方据此触发提示音;返回 false 表示无上升沿(仍可能是 WARN/OK)。
bool nav_state_apply(nav_state_t *s, const nav_update_t *u);
