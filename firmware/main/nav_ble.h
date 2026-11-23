// main/nav_ble.h —— 导航副屏 BLE 外设(GATT 服务 + 可连接广播)。
// 手机作为 central 连接后, 向 NAV 特征值写入导航帧(见 nav_protocol.h)。
#pragma once

#include "esp_err.h"
#include "nav_app.h"

#define NAV_BLE_DEVICE_NAME "PassportNav"

// 启动 BLE 外设。app 的 state_lock / events 须已由 nav_app_init 创建。
// 返回 ESP_OK 表示 NimBLE host 已启动并开始广播; 连接由 NimBLE 异步处理。
esp_err_t nav_ble_start(nav_app_t *app);

// 停止并释放 NimBLE host。幂等, 未启动时返回 ESP_OK。
esp_err_t nav_ble_stop(void);

// 当前是否有手机连接。
bool nav_ble_is_connected(void);
