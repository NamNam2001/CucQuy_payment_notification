/**
 * @file wifi.cc
 * @brief WiFi Manager wrapper dùng 78/esp-wifi-connect.
 *
 * Tham chiếu: xiaozhi-esp32/main/boards/common/wifi_board.cc
 *   - WifiManager::Initialize (tự NVS + netif + event loop + wifi driver)
 *   - SsidManager check có SSID saved không
 *   - StartStation hoặc StartConfigAp
 *   - 60s timeout fallback -> config AP
 *   - Event callback: Connected -> start MQTT
 */
#include "wifi.h"
#include "config.h"
#include "display/display.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>
#include <ssid_manager.h>

static const char *TAG = "wifi";

static wifi_connected_cb_t    s_connected_cb    = nullptr;
static wifi_disconnected_cb_t s_disconnected_cb = nullptr;
static esp_timer_handle_t     s_connect_timer   = nullptr;
static bool                    s_connected       = false;

// ---------------------------------------------------------------------------
// 60s timeout -> nếu station không connect được -> vào config AP
// ---------------------------------------------------------------------------
static void connect_timeout_callback(void *arg)
{
    ESP_LOGW(TAG, "Station connect timeout %ds -> vào config AP", WIFI_CONNECT_TIMEOUT_SEC);
    WifiManager::GetInstance().StopStation();
    WifiManager::GetInstance().StartConfigAp();
}

// ---------------------------------------------------------------------------
// WiFi event callback (chạy trong WiFi event task)
// ---------------------------------------------------------------------------
static void on_wifi_event(WifiEvent event, const std::string &data)
{
    switch (event) {
    case WifiEvent::Connected:
        ESP_LOGI(TAG, "Connected to '%s'", data.c_str());
        s_connected = true;
        display_set_wifi_status(true);           // icon WiFi xanh
        if (s_connect_timer) esp_timer_stop(s_connect_timer);
        if (s_connected_cb) s_connected_cb();
        break;

    case WifiEvent::Disconnected:
        ESP_LOGW(TAG, "Disconnected (reason %s)", data.c_str());
        s_connected = false;
        display_set_wifi_status(false);          // icon WiFi đỏ/X
        if (s_disconnected_cb) s_disconnected_cb();
        break;

    case WifiEvent::ConfigModeEnter:
        ESP_LOGI(TAG, "Config AP mode: AP='%s' URL=%s",
                 WifiManager::GetInstance().GetApSsid().c_str(),
                 WifiManager::GetInstance().GetApWebUrl().c_str());
        // Hiển thị màn config mode cho user biết
        display_show_config_mode(WifiManager::GetInstance().GetApSsid().c_str());
        break;

    case WifiEvent::ConfigModeExit:
        // User đã submit form -> retry station với SSID mới
        ESP_LOGI(TAG, "Config exit -> retry station");
        display_show_home();                     // về màn Home
        display_set_wifi_status(false);
        esp_timer_start_once(s_connect_timer, (uint64_t)WIFI_CONNECT_TIMEOUT_SEC * 1000000ULL);
        WifiManager::GetInstance().StartStation();
        break;

    case WifiEvent::Scanning:
    case WifiEvent::Connecting:
        break;
    }
}

// ---------------------------------------------------------------------------
// Task cho enter_config_mode (tránh race condition)
// ---------------------------------------------------------------------------
static void enter_config_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(200));
    if (s_connect_timer) esp_timer_stop(s_connect_timer);
    WifiManager::GetInstance().StopStation();
    WifiManager::GetInstance().StartConfigAp();
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t wifi_init(void)
{
    ESP_LOGI(TAG, "Init WiFi manager (prefix=%s)", WIFI_AP_PREFIX);

    // 1. Initialize WifiManager (tự NVS + netif + wifi driver)
    WifiManagerConfig cfg;
    cfg.ssid_prefix = WIFI_AP_PREFIX;
    cfg.language     = "en-US";
    if (!WifiManager::GetInstance().Initialize(cfg)) {
        ESP_LOGE(TAG, "WifiManager Initialize failed");
        return ESP_FAIL;
    }

    // 2. Tạo 60s connect timeout timer
    esp_timer_create_args_t timer_args = {
        .callback = connect_timeout_callback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "wifi_timeout",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_connect_timer));

    // 3. Register event callback
    WifiManager::GetInstance().SetEventCallback(on_wifi_event);

    // 4. Decision: có SSID trong NVS?
    bool have_ssid = !SsidManager::GetInstance().GetSsidList().empty();
    if (have_ssid) {
        ESP_LOGI(TAG, "Có SSID trong NVS -> StartStation");
        esp_timer_start_once(s_connect_timer, (uint64_t)WIFI_CONNECT_TIMEOUT_SEC * 1000000ULL);
        WifiManager::GetInstance().StartStation();
    } else {
        ESP_LOGI(TAG, "Chưa có SSID -> StartConfigAp (captive portal)");
        // Chờ 1.5s cho display kịp hiện boot screen
        vTaskDelay(pdMS_TO_TICKS(1500));
        WifiManager::GetInstance().StartConfigAp();
    }

    // 5. Set TX power MAX (20 dBm) cho cả station và AP
    // ESP32 max = 20dBm (value 80). Mặc định có thể thấp hơn -> WiFi yếu.
    esp_wifi_set_max_tx_power(80);
    ESP_LOGI(TAG, "WiFi TX power set to MAX (20 dBm)");

    return ESP_OK;
}

void wifi_enter_config_mode(void)
{
    ESP_LOGI(TAG, "Enter config mode (từ nút bấm)");
    xTaskCreate(enter_config_task, "wifi_cfg", 4096, nullptr, 2, nullptr);
}

bool wifi_is_connected(void)
{
    return s_connected;
}

void wifi_on_connected(wifi_connected_cb_t cb)    { s_connected_cb = cb; }
void wifi_on_disconnected(wifi_disconnected_cb_t cb) { s_disconnected_cb = cb; }
