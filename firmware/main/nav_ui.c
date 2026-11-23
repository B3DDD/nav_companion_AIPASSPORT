// main/nav_ui.c —— 导航副屏界面实现。
// 深色主题便于户外强光下阅读; 显示下一航点/距离/偏离/剩余/转向, 右上角电量。
// 偏离等级以颜色区分(绿/黄/红), 进入 ALERT 时可选触发提示音(beep_task)。
#include "nav_ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "nav_ble.h"
#include "nav_format.h"

#include <stdio.h>
#include <string.h>

// 自定义中文字体(GB2312 + 固定文案), 由 lv_font_conv 生成。
LV_FONT_DECLARE(nav_cjk);

static const char *TAG = "nav_ui";

#define NAV_COLOR_BG     0x10141A
#define NAV_COLOR_FG     0xE8EDF2
#define NAV_COLOR_ACCENT 0x38C6FF
#define NAV_COLOR_MUTED  0x8A94A6
#define NAV_COLOR_OK     0x39D98A
#define NAV_COLOR_WARN   0xFFB020
#define NAV_COLOR_ALERT  0xFF4D4F

// 告警提示音: 880Hz 方波, 120ms x 3 声。
#define BEEP_RATE_HZ     16000
#define BEEP_FREQ_HZ     880
#define BEEP_DURATION_MS 120
#define BEEP_REPEAT      3
#define BEEP_GAP_MS      80
#define BEEP_SAMPLES     (BEEP_RATE_HZ * BEEP_DURATION_MS / 1000)

static nav_app_t *s_app;
static lv_timer_t *s_timer;
static lv_obj_t *s_status;
static lv_obj_t *s_battery;
static lv_obj_t *s_wp;
static lv_obj_t *s_dist;
static lv_obj_t *s_off;
static lv_obj_t *s_remain;
static lv_obj_t *s_turn;
static lv_obj_t *s_alt;
// ---- 数据页 ----
static lv_obj_t *s_d_cum;
static lv_obj_t *s_d_asc;
static lv_obj_t *s_d_cp;
static lv_obj_t *s_d_remain;
static lv_obj_t *s_d_alt;
static lv_obj_t *s_d_total;
static lv_obj_t *s_hint;
static int s_page;              // 0 = 导航页, 1 = 数据页
// ---- 进度条 + CP 刻度 ----
#define NAV_BAR_W 204
#define NAV_BAR_X 18
#define NAV_BAR_Y 262
static lv_obj_t *s_bar;
static lv_obj_t *s_ticks[NAV_CPS_MAX];
static lv_font_t s_font_cjk;   // CJK 16 + Montserrat 16 fallback
static bool s_dimmed;
// 最后一次有效定位的数据: 无定位时保留显示, 只把状态改成警示色。
static nav_update_t s_last_good;
static bool s_has_good;

static uint32_t off_color(nav_off_level_t level)
{
    switch (level) {
    case NAV_OFF_WARN:
        return NAV_COLOR_WARN;
    case NAV_OFF_ALERT:
        return NAV_COLOR_ALERT;
    default:
        return NAV_COLOR_OK;
    }
}

static void fonts_init(void)
{
    // 使用自定义 GB2312 中文字体; 数字/拉丁等缺失字形回退到 Montserrat。
    s_font_cjk = nav_cjk;
    s_font_cjk.fallback = &lv_font_montserrat_16;
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(scr, lv_color_hex(NAV_COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_screen_load(scr);

    s_battery = lv_label_create(scr);
    lv_obj_set_style_text_font(s_battery, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_battery, lv_color_hex(NAV_COLOR_MUTED), 0);
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -16, 12);

    s_status = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_status, lv_color_hex(NAV_COLOR_MUTED), 0);
    lv_obj_align(s_status, LV_ALIGN_TOP_LEFT, 16, 12);

    s_wp = lv_label_create(scr);
    lv_obj_set_style_text_font(s_wp, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_wp, lv_color_hex(NAV_COLOR_ACCENT), 0);
    lv_obj_set_style_text_align(s_wp, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_wp, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wp, 208);
    lv_obj_set_height(s_wp, 44);
    lv_obj_align(s_wp, LV_ALIGN_TOP_MID, 0, 52);

    s_dist = lv_label_create(scr);
    lv_obj_set_style_text_font(s_dist, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_dist, lv_color_hex(NAV_COLOR_FG), 0);
    lv_obj_align(s_dist, LV_ALIGN_TOP_MID, 0, 108);

    s_off = lv_label_create(scr);
    lv_obj_set_style_text_font(s_off, &s_font_cjk, 0);
    lv_obj_align(s_off, LV_ALIGN_TOP_MID, 0, 144);

    s_remain = lv_label_create(scr);
    lv_obj_set_style_text_font(s_remain, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_remain, lv_color_hex(NAV_COLOR_FG), 0);
    lv_obj_align(s_remain, LV_ALIGN_TOP_MID, 0, 174);

    s_turn = lv_label_create(scr);
    lv_obj_set_style_text_font(s_turn, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_turn, lv_color_hex(NAV_COLOR_FG), 0);
    lv_obj_align(s_turn, LV_ALIGN_TOP_MID, 0, 204);

    s_alt = lv_label_create(scr);
    lv_obj_set_style_text_font(s_alt, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_alt, lv_color_hex(NAV_COLOR_ACCENT), 0);
    lv_obj_align(s_alt, LV_ALIGN_TOP_MID, 0, 234);

    // ---- 数据页（默认隐藏，UP/DOWN 切换）----
    s_d_cum = lv_label_create(scr);
    lv_obj_set_style_text_font(s_d_cum, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_d_cum, lv_color_hex(NAV_COLOR_ACCENT), 0);
    lv_obj_align(s_d_cum, LV_ALIGN_TOP_MID, 0, 48);

    s_d_asc = lv_label_create(scr);
    lv_obj_set_style_text_font(s_d_asc, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_d_asc, lv_color_hex(NAV_COLOR_FG), 0);
    lv_obj_align(s_d_asc, LV_ALIGN_TOP_MID, 0, 88);

    s_d_cp = lv_label_create(scr);
    lv_obj_set_style_text_font(s_d_cp, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_d_cp, lv_color_hex(0xFFD24A), 0);
    lv_obj_align(s_d_cp, LV_ALIGN_TOP_MID, 0, 128);

    s_d_remain = lv_label_create(scr);
    lv_obj_set_style_text_font(s_d_remain, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_d_remain, lv_color_hex(NAV_COLOR_FG), 0);
    lv_obj_align(s_d_remain, LV_ALIGN_TOP_MID, 0, 168);

    s_d_alt = lv_label_create(scr);
    lv_obj_set_style_text_font(s_d_alt, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_d_alt, lv_color_hex(NAV_COLOR_ACCENT), 0);
    lv_obj_align(s_d_alt, LV_ALIGN_TOP_MID, 0, 208);

    s_d_total = lv_label_create(scr);
    lv_obj_set_style_text_font(s_d_total, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_d_total, lv_color_hex(NAV_COLOR_MUTED), 0);
    lv_obj_align(s_d_total, LV_ALIGN_TOP_MID, 0, 248);

    // ---- 进度条 + CP 刻度（仅导航页显示）----
    s_bar = lv_bar_create(scr);
    lv_obj_set_size(s_bar, NAV_BAR_W, 10);
    lv_obj_align(s_bar, LV_ALIGN_TOP_LEFT, NAV_BAR_X, NAV_BAR_Y);
    lv_bar_set_range(s_bar, 0, 1000);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x263140), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar, lv_color_hex(NAV_COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_bar, 5, LV_PART_INDICATOR);

    for (int i = 0; i < NAV_CPS_MAX; i++) {
        lv_obj_t *t = lv_obj_create(scr);
        lv_obj_remove_flag(t, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(t, 4, 22);
        lv_obj_set_style_bg_color(t, lv_color_hex(0xFFD24A), 0);
        lv_obj_set_style_bg_opa(t, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(t, 0, 0);
        lv_obj_set_style_radius(t, 1, 0);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        s_ticks[i] = t;
    }

    s_hint = lv_label_create(scr);
    lv_obj_set_style_text_font(s_hint, &s_font_cjk, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(NAV_COLOR_MUTED), 0);
    lv_label_set_text(s_hint, "OK亮度 ↓数据");
    lv_obj_align(s_hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// 仅在文本真正变化时才更新 label, 避免每 tick 重绘导致闪烁。
static void set_text_if_changed(lv_obj_t *label, const char *txt)
{
    const char *cur = lv_label_get_text(label);
    if (cur != NULL && strcmp(cur, txt) == 0) return;
    lv_label_set_text(label, txt);
}

// 按当前页切换两组标签的可见性。状态行/电量/底部提示始终显示。
static void apply_page_visibility(void)
{
    const bool data = (s_page == 1);
    lv_obj_t *nav_labels[] = { s_wp, s_dist, s_off, s_remain, s_turn, s_alt, s_bar };
    for (size_t i = 0; i < sizeof(nav_labels) / sizeof(nav_labels[0]); i++) {
        if (data) lv_obj_add_flag(nav_labels[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(nav_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *data_labels[] = { s_d_cum, s_d_asc, s_d_cp, s_d_remain, s_d_alt, s_d_total };
    for (size_t i = 0; i < sizeof(data_labels) / sizeof(data_labels[0]); i++) {
        if (data) lv_obj_remove_flag(data_labels[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(data_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
    set_text_if_changed(s_hint, data ? "↑ 导航" : "OK亮度 ↓数据");
}

// 进度条与 CP 刻度：u == NULL 表示尚无数据，全部隐藏。
static void update_bar(const nav_update_t *u)
{
    static int last_count = -1;
    const int count = (u != NULL) ? u->cps_count : 0;
    if (count != last_count) {
        last_count = count;
        ESP_LOGI(TAG, "CP 刻度: %d 个, prog_pm=%d", count, u != NULL ? (int)u->prog_pm : -1);
    }

    if (u != NULL && u->prog_pm >= 0) {
        lv_bar_set_value(s_bar, u->prog_pm > 1000 ? 1000 : (int)u->prog_pm, LV_ANIM_OFF);
    }
    for (int i = 0; i < NAV_CPS_MAX; i++) {
        if (u != NULL && s_page == 0 && i < count) {
            int pm = (int)u->cps_pm[i];
            if (pm < 0) pm = 0;
            if (pm > 1000) pm = 1000;
            int x = NAV_BAR_X + (pm * NAV_BAR_W) / 1000;
            lv_obj_align(s_ticks[i], LV_ALIGN_TOP_LEFT, x - 2, NAV_BAR_Y - 6);
            lv_obj_remove_flag(s_ticks[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ticks[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void refresh(lv_timer_t *timer)
{
    nav_state_t snap;
    char dist_buf[16];
    char off_buf[16];
    char remain_buf[16];
    char alt_buf[24];
    char soc_buf[16];
    char joined[32];
    int soc;

    (void)timer;
    if (s_app == NULL || s_app->state_lock == NULL) return;

    if (xSemaphoreTake(s_app->state_lock, pdMS_TO_TICKS(20)) != pdTRUE) return;
    snap = s_app->state;
    xSemaphoreGive(s_app->state_lock);

    soc = bsp_battery_soc();
    if (soc >= 0) {
        snprintf(soc_buf, sizeof(soc_buf), "%d%%", soc);
        set_text_if_changed(s_battery, soc_buf);
    } else {
        set_text_if_changed(s_battery, "--");
    }

    if (!snap.has_data) {
        set_text_if_changed(s_status, nav_ble_is_connected() ? "已连接" : "等待手机连接");
        lv_obj_set_style_text_color(s_status, lv_color_hex(NAV_COLOR_MUTED), 0);
        set_text_if_changed(s_wp, "导航副屏");
        set_text_if_changed(s_dist, "--");
        set_text_if_changed(s_off, "偏离 --");
        lv_obj_set_style_text_color(s_off, lv_color_hex(NAV_COLOR_MUTED), 0);
        set_text_if_changed(s_remain, "剩余 --");
        set_text_if_changed(s_turn, "");
        set_text_if_changed(s_alt, "");
        set_text_if_changed(s_d_cum, "累计 --");
        set_text_if_changed(s_d_asc, "爬升 --");
        set_text_if_changed(s_d_cp, "CP --");
        set_text_if_changed(s_d_remain, "剩余 --");
        set_text_if_changed(s_d_alt, "海拔 --");
        set_text_if_changed(s_d_total, "路线 --");
        update_bar(NULL);
        return;
    }

    // 有定位时刷新"最后有效数据"; 无定位时沿用上次有效值, 只把状态/偏离改成警示色。
    if (snap.update.fix) {
        s_last_good = snap.update;
        s_has_good = true;
    }
    const nav_update_t *u = &snap.update;
    if (!snap.update.fix && s_has_good) {
        u = &s_last_good;
    }

    const bool stale = !snap.update.fix;
    const int st = snap.update.state;

    // ---- 状态行：待出发 / 已出发 / 已到达 ----
    if (st == 0) {
        if (snap.update.remain_m < 0) {
            set_text_if_changed(s_status, "等待 GPS 定位");
        } else {
            nav_format_meters(dist_buf, sizeof(dist_buf), snap.update.remain_m);
            snprintf(joined, sizeof(joined), "距起点 %s", dist_buf);
            set_text_if_changed(s_status, joined);
        }
        lv_obj_set_style_text_color(s_status, lv_color_hex(NAV_COLOR_WARN), 0);
    } else if (st == 2) {
        set_text_if_changed(s_status, "已到达终点");
        lv_obj_set_style_text_color(s_status, lv_color_hex(NAV_COLOR_OK), 0);
    } else {
        set_text_if_changed(s_status, stale ? "无定位" : "已出发");
        lv_obj_set_style_text_color(s_status, lv_color_hex(stale ? NAV_COLOR_WARN : NAV_COLOR_OK), 0);
    }

    // ---- 导航页 ----
    if (st == 0) {
        // 未出发：只显示到达起点的提示，不展示会误导的里程
        set_text_if_changed(s_wp, "走到起点 100m 内");
        set_text_if_changed(s_dist, "--");
        set_text_if_changed(s_off, "偏离 --");
        lv_obj_set_style_text_color(s_off, lv_color_hex(NAV_COLOR_MUTED), 0);
        set_text_if_changed(s_remain, "剩余 --");
        set_text_if_changed(s_turn, "");
    } else {
        set_text_if_changed(s_wp, u->wp[0] != '\0' ? u->wp : "--");

        nav_format_meters(dist_buf, sizeof(dist_buf), u->dist_m);
        set_text_if_changed(s_dist, dist_buf);

        nav_format_meters(off_buf, sizeof(off_buf), u->off_m);
        snprintf(joined, sizeof(joined), "偏离 %s", off_buf);
        set_text_if_changed(s_off, joined);
        lv_obj_set_style_text_color(s_off,
            lv_color_hex(stale ? NAV_COLOR_WARN : off_color(nav_off_level(u->off_m))), 0);

        nav_format_meters(remain_buf, sizeof(remain_buf), u->remain_m);
        snprintf(joined, sizeof(joined), "剩余 %s", remain_buf);
        set_text_if_changed(s_remain, joined);

        set_text_if_changed(s_turn, u->turn[0] != '\0' ? u->turn : "");
    }

    if (u->alt_m >= 0) {
        snprintf(alt_buf, sizeof(alt_buf), "海拔 %dm", (int)u->alt_m);
        set_text_if_changed(s_alt, alt_buf);
        set_text_if_changed(s_d_alt, alt_buf);
    } else {
        set_text_if_changed(s_alt, "");
        set_text_if_changed(s_d_alt, "海拔 --");
    }

    // ---- 数据页 ----
    if (u->cum_m >= 0) {
        nav_format_meters(dist_buf, sizeof(dist_buf), u->cum_m);
        snprintf(joined, sizeof(joined), "累计 %s", dist_buf);
        set_text_if_changed(s_d_cum, joined);
    } else {
        set_text_if_changed(s_d_cum, "累计 --");
    }
    if (u->asc_m >= 0) {
        snprintf(alt_buf, sizeof(alt_buf), "爬升 %dm", (int)u->asc_m);
        set_text_if_changed(s_d_asc, alt_buf);
    } else {
        set_text_if_changed(s_d_asc, "爬升 --");
    }
    if (u->cps_count > 0) {
        snprintf(joined, sizeof(joined), "CP %d个", u->cps_count);
        set_text_if_changed(s_d_cp, joined);
    } else {
        set_text_if_changed(s_d_cp, "CP --");
    }
    nav_format_meters(remain_buf, sizeof(remain_buf), u->remain_m);
    snprintf(joined, sizeof(joined), "剩余 %s", remain_buf);
    set_text_if_changed(s_d_remain, joined);
    if (u->total_m >= 0) {
        nav_format_meters(dist_buf, sizeof(dist_buf), u->total_m);
        snprintf(joined, sizeof(joined), "路线 %s", dist_buf);
        set_text_if_changed(s_d_total, joined);
    } else {
        set_text_if_changed(s_d_total, "路线 --");
    }

    update_bar(u);
}

static void beep_task(void *arg)
{
    nav_app_t *app = (nav_app_t *)arg;
    static int16_t pcm[BEEP_SAMPLES];
    const size_t period = BEEP_RATE_HZ / BEEP_FREQ_HZ;

    for (size_t i = 0; i < BEEP_SAMPLES; i++) {
        pcm[i] = ((i % period) < (period / 2)) ? 8000 : -8000;
    }

    for (;;) {
        xEventGroupWaitBits(app->events, NAV_EVT_ALERT, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bsp_audio_set_format(BEEP_RATE_HZ, 16, 1) != ESP_OK) {
            ESP_LOGW(TAG, "提示音格式设置失败");
            continue;
        }
        for (int n = 0; n < BEEP_REPEAT; n++) {
            bsp_audio_write(pcm, BEEP_SAMPLES * sizeof(int16_t));
            vTaskDelay(pdMS_TO_TICKS(BEEP_GAP_MS));
        }
    }
}

void nav_ui_init(nav_app_t *app, bool audio_available)
{
    s_app = app;
    s_dimmed = false;
    s_page = 0;
    fonts_init();
    build_ui();
    apply_page_visibility();
    s_timer = lv_timer_create(refresh, 500, NULL);
    refresh(NULL);   // 立即填充一次, 避免首屏空白

    if (audio_available && app != NULL && app->events != NULL) {
        if (xTaskCreate(beep_task, "nav_beep", 4096, app, 5, NULL) != pdPASS) {
            ESP_LOGW(TAG, "提示音任务创建失败");
        }
    }
}

void nav_ui_on_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) {
        s_dimmed = !s_dimmed;
        bsp_display_backlight(s_dimmed ? 25 : 100);
        return;
    }
    // 上下键: 导航页 <-> 数据页。回调在 esp_timer 任务里, 改 LVGL 对象必须持锁。
    if (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN) {
        if (!bsp_lvgl_lock(200)) return;
        s_page = (s_page == 0) ? 1 : 0;
        apply_page_visibility();
        refresh(NULL);
        bsp_lvgl_unlock();
    }
}
