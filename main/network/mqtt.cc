/**
 * @file mqtt.cc
 * @brief MQTT client dùng esp-mqtt (built-in ESP-IDF).
 *
 * Thiết kế:
 *   - mqtt_init() start client rồi return ngay. esp-mqtt tự reconnect nền.
 *   - mqtt_init() CHỜ connected tối đa timeout_ms, nhưng KHÔNG fail/crash.
 *   - Khi reconnect, on_connected callback tự re-subscribe.
 */
#include "mqtt.h"
#include "config.h"

#include <esp_log.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <mqtt_client.h>
#include <esp_crt_bundle.h>

static const char *TAG = "mqtt";

#define MQTT_CONNECTED_BIT   BIT0
#define MQTT_DISCONNECTED_BIT BIT1

static EventGroupHandle_t s_mqtt_event_group = nullptr;
static esp_mqtt_client_handle_t s_client = nullptr;
static mqtt_message_cb_t s_message_cb = nullptr;
static mqtt_connected_cb_t s_connected_cb = nullptr;   // re-subscribe khi reconnect

// Lưu topic cần subscribe (để re-subscribe khi reconnect)
#define MAX_SUBSCRIBED_TOPICS 8
static char s_subscribed_topics[MAX_SUBSCRIBED_TOPICS][96];
static int  s_subscribed_topics_qos[MAX_SUBSCRIBED_TOPICS];
static int  s_subscribed_count = 0;

static bool s_ever_connected = false;

// Buffer để reassemble message lớn (esp-mqtt giới hạn ~1KB/event)
static char *s_msg_buf = nullptr;
static int   s_msg_buf_len = 0;
static int   s_msg_total_len = 0;
static char  s_topic_buf[128] = {0};

static void resubscribe_all(void)
{
    for (int i = 0; i < s_subscribed_count; i++) {
        int msg_id = esp_mqtt_client_subscribe_single(s_client, s_subscribed_topics[i],
                                                       s_subscribed_topics_qos[i]);
        ESP_LOGI(TAG, "re-subscribe '%s' -> msg_id=%d", s_subscribed_topics[i], msg_id);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_t *event = (esp_mqtt_event_t *)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected%s", s_ever_connected ? " (reconnected)" : "");
        s_ever_connected = true;
        xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
        // Re-subscribe mọi topic (lần đầu + mỗi lần reconnect)
        resubscribe_all();
        if (s_connected_cb) s_connected_cb();
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected - sẽ tự reconnect");
        xEventGroupSetBits(s_mqtt_event_group, MQTT_DISCONNECTED_BIT);
        break;

    case MQTT_EVENT_DATA: {
        int topic_len = event->topic_len;
        if (topic_len >= (int)sizeof(s_topic_buf)) topic_len = sizeof(s_topic_buf) - 1;
        memcpy(s_topic_buf, event->topic, topic_len);
        s_topic_buf[topic_len] = '\0';

        if (event->data_len == event->total_data_len) {
            if (s_message_cb) {
                s_message_cb(s_topic_buf, event->data, event->data_len);
            }
        } else {
            if (s_msg_buf == nullptr) {
                s_msg_total_len = event->total_data_len;
                s_msg_buf_len = 0;
                s_msg_buf = (char *)malloc(s_msg_total_len + 1);
            }
            if (s_msg_buf && s_msg_buf_len + event->data_len <= s_msg_total_len) {
                memcpy(s_msg_buf + s_msg_buf_len, event->data, event->data_len);
                s_msg_buf_len += event->data_len;
                if (s_msg_buf_len >= s_msg_total_len) {
                    s_msg_buf[s_msg_buf_len] = '\0';
                    if (s_message_cb) {
                        s_message_cb(s_topic_buf, s_msg_buf, s_msg_buf_len);
                    }
                    free(s_msg_buf);
                    s_msg_buf = nullptr;
                    s_msg_buf_len = 0;
                }
            }
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        if (event->error_handle) {
            ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
        }
        break;

    default:
        break;
    }
}

esp_err_t mqtt_init(void)
{
    ESP_LOGI(TAG, "Init MQTT client - broker: %s", MQTT_BROKER_URI);

    if (s_mqtt_event_group == nullptr) {
        s_mqtt_event_group = xEventGroupCreate();
    }

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri    = MQTT_BROKER_URI;
    mqtt_cfg.credentials.client_id = MQTT_CLIENT_ID;
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = MQTT_USERNAME;
        mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    }
    // esp-mqtt tự reconnect nền - cấu hình reconnect
    mqtt_cfg.network.reconnect_timeout_ms = 5000;   // retry mỗi 5s
    mqtt_cfg.task.stack_size = 6144;
    mqtt_cfg.buffer.size     = 1024;
    mqtt_cfg.buffer.out_size = 1024;
    mqtt_cfg.network.timeout_ms = 10000;

    if (strncmp(MQTT_BROKER_URI, "mqtts://", 8) == 0) {
        mqtt_cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == nullptr) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client,
                                   (esp_mqtt_event_id_t)MQTT_EVENT_ANY,
                                   mqtt_event_handler, nullptr);
    esp_mqtt_client_start(s_client);

    // Chờ connected tối đa 10s. Nếu timeout KHÔNG fail - esp-mqtt sẽ retry nền.
    EventBits_t bits = xEventGroupWaitBits(s_mqtt_event_group,
                                           MQTT_CONNECTED_BIT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (bits & MQTT_CONNECTED_BIT) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "MQTT chưa connect sau 10s - sẽ tiếp tục retry ở nền");
    return ESP_OK;   // ⚠️ LUÔN return OK - không bao giờ crash app
}

esp_err_t mqtt_subscribe(const char *topic, int qos)
{
    if (topic == nullptr) return ESP_ERR_INVALID_ARG;

    // Lưu lại topic (để re-subscribe khi reconnect)
    if (s_subscribed_count < MAX_SUBSCRIBED_TOPICS) {
        strncpy(s_subscribed_topics[s_subscribed_count], topic,
                sizeof(s_subscribed_topics[0]) - 1);
        s_subscribed_topics[s_subscribed_count][sizeof(s_subscribed_topics[0]) - 1] = '\0';
        s_subscribed_topics_qos[s_subscribed_count] = qos;
        s_subscribed_count++;
    }

    if (s_client == nullptr) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_subscribe_single(s_client, topic, qos);
    ESP_LOGI(TAG, "subscribe '%s' -> msg_id=%d", topic, msg_id);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_publish(const char *topic, const char *payload, int qos)
{
    if (s_client == nullptr || topic == nullptr) return ESP_ERR_INVALID_STATE;
    int msg_id = esp_mqtt_client_publish(s_client, topic, payload,
                                         payload ? (int)strlen(payload) : 0, qos, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}

void mqtt_on_message(mqtt_message_cb_t cb) { s_message_cb = cb; }

void mqtt_on_connected(mqtt_connected_cb_t cb) { s_connected_cb = cb; }

bool mqtt_is_connected(void)
{
    return (s_mqtt_event_group != nullptr) &&
           (xEventGroupGetBits(s_mqtt_event_group) & MQTT_CONNECTED_BIT);
}
