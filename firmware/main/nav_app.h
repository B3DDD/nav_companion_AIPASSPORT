// main/nav_app.h —— 应用级共享对象与跨任务事件位(仅固件,依赖 FreeRTOS)。
#pragma once

#include "nav_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// 事件位:UI 定时器也可直接轮询 state,这里主要给"新数据"和"告警"即时信号。
#define NAV_EVT_DATA  (1 << 0)   // 收到新导航帧
#define NAV_EVT_ALERT (1 << 1)   // 偏离进入 ALERT(触发提示音)

typedef struct {
    nav_state_t state;
    SemaphoreHandle_t state_lock;   // 保护 state
    EventGroupHandle_t events;      // NAV_EVT_* 跨任务信号
} nav_app_t;

// 创建互斥锁与事件组。失败时对应句柄为 NULL,上层须检查。
void nav_app_init(nav_app_t *app);
