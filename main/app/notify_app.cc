/**
 * @file notify_app.cc
 * @brief 2-topic workflow: order/create (show QR) + order/paid (báo loa).
 *
 * Topic 1: cucquy/esp_01/order/create  {"order_id","amount","qr"}
 *   → ESP32 hiển thị QR + số tiền (im lặng)
 *
 * Topic 2: cucquy/esp_01/order/paid    {"order_id","amount","sender"}
 *   → ESP32 báo loa + TTS + về màn Home
 */
#include "notify_app.h"
#include "config.h"
#include "display/display.h"
#include "audio/audio.h"
#include "network/mqtt.h"
#include "network/tts.h"

#include <esp_log.h>
#include <cJSON.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

// Embedded MP3 "tinh tinh"
extern const char tinh_tinh_mp3_start[] asm("_binary_tinh_tinh_mp3_start");
extern const char tinh_tinh_mp3_end[]   asm("_binary_tinh_tinh_mp3_end");

static const char *TAG = "notify";

// ---------------------------------------------------------------------------
// State: đơn hàng hiện đang chờ thanh toán
// ---------------------------------------------------------------------------
typedef enum { STATE_HOME, STATE_WAITING_PAYMENT } app_state_t;
static app_state_t s_state = STATE_HOME;
static char  s_current_order_id[32] = {0};
static int   s_current_amount = 0;

// Queue cho paid event (defer từ MQTT task sang notify task)
typedef struct {
    int  amount;
} paid_msg_t;
static QueueHandle_t s_paid_queue = nullptr;

// ---------------------------------------------------------------------------
// Parse helper
// ---------------------------------------------------------------------------
static bool parse_json_field(const char *payload, int len, const char *key, char *out, size_t out_size)
{
    char *js = (char *)malloc(len + 1);
    if (!js) return false;
    memcpy(js, payload, len);
    js[len] = '\0';
    cJSON *root = cJSON_Parse(js);
    if (!root) { free(js); return false; }
    cJSON *item = cJSON_GetObjectItem(root, key);
    bool ok = false;
    if (cJSON_IsString(item)) {
        strncpy(out, item->valuestring, out_size - 1);
        out[out_size - 1] = '\0';
        ok = true;
    }
    cJSON_Delete(root);
    free(js);
    return ok;
}

static long parse_json_amount(const char *payload, int len)
{
    char *js = (char *)malloc(len + 1);
    if (!js) return -1;
    memcpy(js, payload, len);
    js[len] = '\0';
    cJSON *root = cJSON_Parse(js);
    if (!root) { free(js); return -1; }
    cJSON *item = cJSON_GetObjectItem(root, "amount");
    long val = cJSON_IsNumber(item) ? item->valueint : -1;
    cJSON_Delete(root);
    free(js);
    return val;
}

// ---------------------------------------------------------------------------
// Handler: order/create → show QR (im lặng)
// ---------------------------------------------------------------------------
static void handle_order_create(const char *payload, int len)
{
    char qr[256] = {0};
    char order_id[32] = {0};
    long amount = parse_json_amount(payload, len);
    parse_json_field(payload, len, "qr", qr, sizeof(qr));
    parse_json_field(payload, len, "order_id", order_id, sizeof(order_id));

    ESP_LOGI(TAG, "[CREATE] order=%s amount=%ld qr_len=%d", order_id, amount, (int)strlen(qr));

    // Lưu state
    strncpy(s_current_order_id, order_id, sizeof(s_current_order_id) - 1);
    s_current_amount = (int)amount;
    s_state = STATE_WAITING_PAYMENT;

    // Hiển thị QR + số tiền + mã đơn (KHÔNG loa)
    display_show_qr(qr[0] ? qr : nullptr, (int)amount, order_id);
}

// ---------------------------------------------------------------------------
// Handler: order/paid → báo loa + TTS + về Home (defer sang notify_task)
// ---------------------------------------------------------------------------
static void handle_order_paid(const char *payload, int len)
{
    char order_id[32] = {0};
    long amount = parse_json_amount(payload, len);
    paid_msg_t msg = {};
    msg.amount = (int)amount;
    parse_json_field(payload, len, "order_id", order_id, sizeof(order_id));

    ESP_LOGI(TAG, "[PAID] order=%s amount=%ld", order_id, amount);

    // So khớp order_id
    if (s_state != STATE_WAITING_PAYMENT ||
        strcmp(order_id, s_current_order_id) != 0) {
        ESP_LOGW(TAG, "Order không khớp hoặc không đang chờ - bỏ qua (cur=%s recv=%s)",
                 s_current_order_id, order_id);
        return;
    }

    // Push vào queue để notify_task xử lý audio
    if (s_paid_queue) {
        xQueueSend(s_paid_queue, &msg, 0);
    }
}

// ---------------------------------------------------------------------------
// MQTT callback dispatcher
// ---------------------------------------------------------------------------
static void on_mqtt_message(const char *topic, const char *payload, int payload_len)
{
    if (strcmp(topic, MQTT_TOPIC_ORDER_CREATE) == 0) {
        handle_order_create(payload, payload_len);
    } else if (strcmp(topic, MQTT_TOPIC_ORDER_PAID) == 0) {
        handle_order_paid(payload, payload_len);
    }
}

// ---------------------------------------------------------------------------
// Notify task: xử lý audio khi paid (tinh tinh + TTS)
// ---------------------------------------------------------------------------
static void notify_task(void *arg)
{
    paid_msg_t msg;
    while (1) {
        if (xQueueReceive(s_paid_queue, &msg, portMAX_DELAY) != pdPASS) continue;

        // 1. 🔔 tinh tinh
        audio_enable_output(true);
        tts_play_embedded_mp3((const uint8_t *)tinh_tinh_mp3_start,
                              tinh_tinh_mp3_end - tinh_tinh_mp3_start);

        // 2. 🗣️ TTS stream "Đã nhận X đồng" (không đọc tên sender)
        char tts_text[100];
        snprintf(tts_text, sizeof(tts_text), "Đã nhận %d đồng", msg.amount);
        ESP_LOGI(TAG, "TTS: %s", tts_text);
        esp_err_t tts_ret = tts_speak(tts_text);
        if (tts_ret != ESP_OK) {
            audio_play_beep(880, 150);
            vTaskDelay(pdMS_TO_TICKS(80));
            audio_play_beep(1320, 200);
        }

        // 3. Tắt I2S
        vTaskDelay(pdMS_TO_TICKS(50));
        audio_enable_output(false);

        // 4. Clear state + về màn Home
        s_current_order_id[0] = '\0';
        s_current_amount = 0;
        s_state = STATE_HOME;
        display_show_home();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t notify_app_init(void)
{
    s_paid_queue = xQueueCreate(4, sizeof(paid_msg_t));
    if (!s_paid_queue) return ESP_ERR_NO_MEM;

    BaseType_t ok = xTaskCreate(notify_task, "notify", 16384, nullptr, 5, nullptr);
    if (ok != pdPASS) return ESP_FAIL;

    mqtt_on_message(on_mqtt_message);
    mqtt_subscribe(MQTT_TOPIC_ORDER_CREATE, 1);
    mqtt_subscribe(MQTT_TOPIC_ORDER_PAID, 1);

    ESP_LOGI(TAG, "Subscribed: %s + %s", MQTT_TOPIC_ORDER_CREATE, MQTT_TOPIC_ORDER_PAID);
    return ESP_OK;
}
