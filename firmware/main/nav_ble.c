// main/nav_ble.c —— NimBLE GATT 外设实现。
// 结构沿用基线 demo_ble.c 的 host 生命周期(start/stop/同步/复位), 并在此基础上
// 增加 GATT 服务注册与可连接广播。UUID 采用 128-bit, 与 Android 端保持一致。
#include "nav_ble.h"
#include "nav_protocol.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "nav_ble";

#define NAV_BLE_STOP_TIMEOUT_MS 2000
#define NAV_BLE_VERSION "PassportNav/0.2"

// 128-bit UUID(规范形式):
//   服务  6E40AB00-B5A3-F393-E0A9-E50E24DCCA9E
//   NAV  6E40AB01-B5A3-F393-E0A9-E50E24DCCA9E (Write / Write No Response)
//   STAT 6E40AB02-B5A3-F393-E0A9-E50E24DCCA9E (Read, 返回版本串)
// NimBLE BLE_UUID128_INIT 接收的是规范形式按字节倒序后的序列。
#define NAV_SERVICE_UUID_BYTES                                                  \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,    \
        0x00, 0xab, 0x40, 0x6e
#define NAV_CHR_UUID_BYTES                                                      \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,    \
        0x01, 0xab, 0x40, 0x6e
#define NAV_STAT_UUID_BYTES                                                     \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3, 0xa3, 0xb5,    \
        0x02, 0xab, 0x40, 0x6e

static nav_app_t *s_app;
static nav_rx_t s_rx;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(NAV_SERVICE_UUID_BYTES);
static const ble_uuid128_t s_nav_uuid = BLE_UUID128_INIT(NAV_CHR_UUID_BYTES);
static const ble_uuid128_t s_stat_uuid = BLE_UUID128_INIT(NAV_STAT_UUID_BYTES);

// host 生命周期状态(与基线 demo_ble.c 一致)
static SemaphoreHandle_t s_host_stopped;
static TaskHandle_t s_host_task;
static uint8_t s_own_addr_type;
static bool s_initialized;
static bool s_start_requested;
static bool s_stop_in_progress;
static bool s_host_done;
static int s_error;
static volatile bool s_connected;

static int nav_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int stat_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);
static int advertise(void);
static int gap_event(struct ble_gap_event *event, void *arg);

// 用独立的静态数组, 避免文件作用域复合字面量在不同编译器下的存储期歧义。
static const struct ble_gatt_chr_def s_nav_chrs[] = {
    {
        .uuid = &s_nav_uuid.u,
        .access_cb = nav_chr_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &s_stat_uuid.u,
        .access_cb = stat_chr_access,
        .flags = BLE_GATT_CHR_F_READ,
    },
    {0},
};

static const struct ble_gatt_svc_def s_gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = s_nav_chrs,
    },
    {0},
};

static void nav_apply_update(const nav_update_t *u)
{
    bool alert = false;
    if (s_app != NULL && s_app->state_lock != NULL &&
        xSemaphoreTake(s_app->state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        alert = nav_state_apply(&s_app->state, u);
        xSemaphoreGive(s_app->state_lock);
    }
    if (s_app != NULL && s_app->events != NULL) {
        xEventGroupSetBits(s_app->events, NAV_EVT_DATA);
        if (alert) {
            xEventGroupSetBits(s_app->events, NAV_EVT_ALERT);
        }
    }
}

static int nav_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *m;
    nav_update_t update;

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    // 一次 GATT 写可能被 mbuf 链拆成多段; 逐段喂给流式解析器。
    for (m = ctxt->om; m != NULL; m = SLIST_NEXT(m, om_next)) {
        if (nav_rx_feed(&s_rx, m->om_data, m->om_len, &update) == NAV_RX_GOT_FRAME) {
            nav_apply_update(&update);
        }
    }
    return 0;
}

static int stat_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    struct os_mbuf *om;

    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    om = ble_hs_mbuf_from_flat((const uint8_t *)NAV_BLE_VERSION,
                               (uint16_t)strlen(NAV_BLE_VERSION));
    if (om == NULL) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    ctxt->om = om;
    return 0;
}

static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp = {0};
    struct ble_gap_adv_params params = {0};
    int rc;

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) return rc;

    rsp.name = (uint8_t *)NAV_BLE_DEVICE_NAME;
    rsp.name_len = (uint8_t)strlen(NAV_BLE_DEVICE_NAME);
    rsp.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp);
    if (rc != 0) return rc;

    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params,
                             gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected = true;
            ESP_LOGI(TAG, "手机已连接 conn=%d", event->connect.conn_handle);
        } else {
            ESP_LOGW(TAG, "连接失败 status=%d", event->connect.status);
            if (s_start_requested) return advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        s_connected = false;
        ESP_LOGI(TAG, "手机已断开 reason=%d", event->disconnect.reason);
        // 连接断开后重新广播, 等待手机下次连接。
        if (s_start_requested) {
            return advertise();
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "特征值订阅 attr=%d notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_start_requested) {
            return advertise();
        }
        break;
    default:
        break;
    }
    return 0;
}

static void on_reset(int reason)
{
    s_error = reason;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc == 0 && s_start_requested) rc = advertise();
    if (rc != 0) {
        s_error = rc;
        ESP_LOGE(TAG, "sync 失败 rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "sync 完成, 广播中");
    }
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    // 不使用 nimble_port_freertos 包装(它会私自持有任务句柄并忽略创建失败);
    // 本模块自管任务的创建、确认与删除。
    xSemaphoreGive(s_host_stopped);
    for (;;) vTaskSuspend(NULL);
}

esp_err_t nav_ble_start(nav_app_t *app)
{
    esp_err_t err;
    int rc;

    if (s_initialized) {
        s_error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    if (app == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_app = app;
    s_error = 0;
    s_stop_in_progress = false;
    s_host_done = false;
    nav_rx_init(&s_rx);

    err = nimble_port_init();
    if (err != ESP_OK) {
        s_error = err;
        return err;
    }
    s_initialized = true;
    s_host_stopped = xSemaphoreCreateBinary();
    if (s_host_stopped == NULL) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();
    rc = ble_svc_gap_device_name_set(NAV_BLE_DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "device name set failed: %d", rc);
        err = ESP_FAIL;
        goto failed_start;
    }

    // GATT 服务必须在 host 任务启动之前注册: ble_gatts_start() 在 host 启动时
    // 构建 ATT 表, 之后再 add_svcs 只会进列表、不会出现在服务发现结果里。
    rc = ble_gatts_count_cfg(s_gatt_services);
    if (rc == 0) rc = ble_gatts_add_svcs(s_gatt_services);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT 服务注册失败 rc=%d", rc);
        err = ESP_FAIL;
        goto failed_start;
    }
    ESP_LOGI(TAG, "GATT 服务已注册");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    s_start_requested = true;
    if (xTaskCreatePinnedToCore(host_task, "nav_ble_host", NIMBLE_HS_STACK_SIZE,
                               NULL, configMAX_PRIORITIES - 4, &s_host_task,
                               NIMBLE_CORE) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto failed_start;
    }
    return ESP_OK;

failed_start:
    s_start_requested = false;
    {
        esp_err_t cleanup = nav_ble_stop();
        if (cleanup != ESP_OK) {
            ESP_LOGE(TAG, "startup cleanup failed: %s", esp_err_to_name(cleanup));
        }
    }
    s_error = err;
    return err;
}

esp_err_t nav_ble_stop(void)
{
    esp_err_t err;

    s_start_requested = false;
    if (!s_initialized) return ESP_OK;

    if (s_host_task != NULL && !s_stop_in_progress) {
        (void)ble_gap_adv_stop();
        if (nimble_port_stop() != 0) {
            ESP_LOGE(TAG, "nimble_port_stop failed");
            s_error = ESP_FAIL;
            return ESP_FAIL;
        }
        s_stop_in_progress = true;
    }

    if (s_host_task != NULL && !s_host_done) {
        if (s_host_stopped == NULL ||
            xSemaphoreTake(s_host_stopped, pdMS_TO_TICKS(NAV_BLE_STOP_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG, "nimble host stop timeout");
            s_error = ESP_ERR_TIMEOUT;
            return ESP_ERR_TIMEOUT;
        }
        s_host_done = true;
    }

    if (s_host_task != NULL) {
        vTaskDelete(s_host_task);
        s_host_task = NULL;
    }

    err = nimble_port_deinit();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_deinit failed: %s", esp_err_to_name(err));
        s_error = err;
        return err;
    }
    s_initialized = false;
    s_stop_in_progress = false;
    s_host_done = false;
    if (s_host_stopped != NULL) {
        vSemaphoreDelete(s_host_stopped);
        s_host_stopped = NULL;
    }
    s_app = NULL;
    return ESP_OK;
}

bool nav_ble_is_connected(void)
{
    return s_connected;
}
