/**
 * @file wifi.cc
 * @brief WiFi Station dùng esp_wifi built-in ESP-IDF.
 *
 * Khác với xiaozhi (dùng 78/esp-wifi-connect), ở đây tự connect trực tiếp
 * với SSID/password hardcode trong config.h. Đơn giản, không phụ thuộc ngoài.
 *
 * Luồng:
 *   nvs_flash_init -> esp_netif_init -> event_loop_create ->
 *   esp_netif_create_default_wifi_sta -> esp_wifi_init ->
 *   register event handler (WIFI_EVENT + IP_EVENT) ->
 *   esp_wifi_set_config -> esp_wifi_start ->
 *   (esp_wifi_connect nội bộ) -> IP_EVENT_STA_GOT_IP -> connected_cb
 */
#include "wifi.h"
#include "config.h"

#include <esp_log.h>
#include <string.h>
#include <nvs_flash.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

static const char *TAG = "wifi";

#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group = nullptr;
static wifi_connected_cb_t    s_connected_cb    = nullptr;
static wifi_disconnected_cb_t s_disconnected_cb = nullptr;
static int s_retry_count = 0;

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            if (s_retry_count < 5) {
                esp_wifi_connect();
                s_retry_count++;
                ESP_LOGW(TAG, "retry connect #%d", s_retry_count);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                if (s_disconnected_cb) s_disconnected_cb();
            }
            break;
        }
        default: break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
            s_retry_count = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            if (s_connected_cb) s_connected_cb();
        }
    }
}

esp_err_t wifi_init(void)
{
    // ---- 1. NVS (esp_wifi cần) ----
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    s_wifi_event_group = xEventGroupCreate();

    // ---- 2. netif + event loop ----
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    // ---- 3. wifi init với default config ----
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // ---- 4. register event handler ----
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        &event_handler, nullptr, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        &event_handler, nullptr, &instance_got_ip));

    // ---- 5. cấu hình STA ----
    wifi_config_t wifi_config = {};
    strncpy((char *)wifi_config.sta.ssid,     WIFI_SSID,     sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASSWORD, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init done - waiting for IP...");

    // ---- 6. Chờ connected hoặc fail (block - gọi 1 lần lúc boot) ----
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s", WIFI_SSID);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "FAILED to connect to SSID:%s", WIFI_SSID);
    return ESP_FAIL;
}

void wifi_on_connected(wifi_connected_cb_t cb)    { s_connected_cb = cb; }
void wifi_on_disconnected(wifi_disconnected_cb_t cb) { s_disconnected_cb = cb; }

bool wifi_is_connected(void)
{
    if (s_wifi_event_group == nullptr) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}
