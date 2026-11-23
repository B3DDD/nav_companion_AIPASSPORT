// main/main.c —— 导航副屏应用入口: 初始化 BSP, 启动 BLE 外设与导航界面。
// 越野跑场景: 手机 App 作为 BLE central 连接并推送导航信息, 本设备作为挂包副屏显示。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nav_app.h"
#include "nav_ble.h"
#include "nav_ui.h"

static const char *TAG = "nav_main";

// 按键回调运行在共享 esp_timer 任务; 只转发非阻塞的背光切换。
static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    nav_ui_on_key(btn, ev);
}

void app_main(void)
{
    // 必须是静态存储: nav_ui 的 LVGL 定时器与 nav_ble 会长期持有 &app,
    // app_main 返回后 main_task 栈会被释放, 局部变量会变成野指针。
    static nav_app_t app;
    esp_err_t nvs_err;

    ESP_LOGI(TAG, "导航副屏启动");

    nav_app_init(&app);

    // 当前 BLE 配置(NIMBLE_NVS_PERSIST=n)不依赖 NVS; 失败不阻塞、不擦除用户数据。
    nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) {
        ESP_LOGW(TAG, "NVS 初始化失败: %s", esp_err_to_name(nvs_err));
    }

    bsp_i2c_init();
    bsp_i2c_scan();

    // 屏幕是 UI 载体, 失败则无法继续。
    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败");
        return;
    }
    bsp_display_backlight(100);

    const bool audio_ok = (bsp_audio_init() == ESP_OK);
    const bool battery_ok = (bsp_battery_init() == ESP_OK);
    if (!audio_ok) ESP_LOGW(TAG, "音频不可用, 告警仅视觉");
    if (!battery_ok) ESP_LOGW(TAG, "电量计不可用");

    // UI 创建必须在 LVGL 锁内。
    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "LVGL 锁超时");
        return;
    }
    nav_ui_init(&app, audio_ok);
    bsp_lvgl_unlock();

    if (bsp_button_init(on_key, NULL) != ESP_OK) {
        ESP_LOGW(TAG, "按键初始化失败");
    }

    if (app.state_lock == NULL || app.events == NULL) {
        ESP_LOGE(TAG, "导航状态资源创建失败");
        return;
    }
    if (nav_ble_start(&app) != ESP_OK) {
        ESP_LOGW(TAG, "BLE 启动失败");
    }

    ESP_LOGI(TAG, "就绪: BLE=%s 音频=%d 电量=%d",
             NAV_BLE_DEVICE_NAME, audio_ok, battery_ok);
}
