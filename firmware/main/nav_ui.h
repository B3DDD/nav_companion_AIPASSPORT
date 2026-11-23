// main/nav_ui.h —— 导航副屏界面(全新设计, 不沿用 demo 测试菜单/外壳)。
#pragma once

#include <stdbool.h>
#include "bsp_button.h"
#include "nav_app.h"

// 创建导航界面、刷新定时器与(可选)告警提示音任务。
// 必须在持有 bsp_lvgl_lock() 时调用(内部会创建并载入 LVGL 对象)。
void nav_ui_init(nav_app_t *app, bool audio_available);

// 按键事件入口(由 bsp_button_init 的回调转发)。非阻塞, 仅切换背光。
void nav_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev);
