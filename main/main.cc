/**
 * @file main.cc
 * @brief Entry point - NamPOS Payment Notification.
 *
 * Luồng khởi tạo MỚI với captive portal:
 *   1. display_init()     - LCD (màn Home "Hello NamPOS")
 *   2. audio_init()       - I2S + TTS decoder
 *   3. wifi_init()        - WifiManager + decision station/config AP
 *        ├── Connected    → mqtt_init() + notify_app_init()
 *        └── Disconnected → retry tự động (esp-wifi-connect lo)
 *   4. Nút GPIO0 giữ 2s   → wifi_enter_config_mode()
 */
#include "display/display.h"
#include "audio/audio.h"
#include "network/wifi.h"
#include "network/mqtt.h"
#include "network/tts.h"
#include "app/notify_app.h"
#include "config.h"

#include <esp_log.h>
#include <esp_err.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

// Forward declarations
static void start_mqtt_and_notify(void);
static void setup_button(void);

// GPIO0 (nút BOOT) - nhấn giữ 2s -> config mode
#define BUTTON_GPIO         BOOT_BUTTON_GPIO
#define BUTTON_LONGPRESS_MS 2000

// ---------------------------------------------------------------------------
// Callback khi WiFi connected -> start MQTT + notify app
// ---------------------------------------------------------------------------
static void on_wifi_connected(void)
{
    ESP_LOGI(TAG, "WiFi connected -> start MQTT + notify");
    start_mqtt_and_notify();
}

static void on_wifi_disconnected(void)
{
    ESP_LOGW(TAG, "WiFi disconnected - MQTT sẽ tự reconnect khi WiFi lại");
}

static void start_mqtt_and_notify(void)
{
    // MQTT (esp-mqtt tự reconnect)
    mqtt_init();
    // Notify app (subscribe 2 topic)
    notify_app_init();

    // Boot beep xác nhận sẵn sàng
    audio_play_beep(660, 100);
    vTaskDelay(pdMS_TO_TICKS(80));
    audio_play_beep(880, 150);

    ESP_LOGI(TAG, "System ready");
    ESP_LOGI(TAG, "  topic create: %s", MQTT_TOPIC_ORDER_CREATE);
    ESP_LOGI(TAG, "  topic paid:   %s", MQTT_TOPIC_ORDER_PAID);
}

// ---------------------------------------------------------------------------
// Button GPIO0: periodic poll, detect long-press 2s
// ---------------------------------------------------------------------------
static void button_poll_task(void *arg)
{
    // Cấu hình GPIO0 input pull-up (nút BOOT active-low)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    int pressed_ms = 0;
    bool triggered = false;
    const int poll_interval_ms = 50;

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        if (level == 0) {   // pressed (active low)
            pressed_ms += poll_interval_ms;
            if (pressed_ms >= BUTTON_LONGPRESS_MS && !triggered) {
                ESP_LOGI(TAG, "Button long-press %dms -> config mode", pressed_ms);
                wifi_enter_config_mode();
                triggered = true;
            }
        } else {
            pressed_ms = 0;
            triggered = false;
        }
        vTaskDelay(pdMS_TO_TICKS(poll_interval_ms));
    }
}

static void setup_button(void)
{
    xTaskCreate(button_poll_task, "btn_poll", 4096, nullptr, 2, nullptr);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== NamPOS Payment Notification ===");
    ESP_LOGI(TAG, "ESP32-WROOM 4MB + ST7735 128x160 + PCM5102A");

    // 1. Display (boot vào màn Home)
    ESP_ERROR_CHECK(display_init());

    // 2. Audio + TTS decoder
    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(tts_init());

    // 3. Setup nút GPIO0 (long-press 2s -> config mode)
    setup_button();

    // 4. WiFi init (WifiManager tự quyết station/config AP)
    wifi_on_connected(on_wifi_connected);
    wifi_on_disconnected(on_wifi_disconnected);
    wifi_init();
    // ← KHÔNG gọi mqtt_init() ở đây nữa. Đợi on_wifi_connected() trigger.

    ESP_LOGI(TAG, "Boot sequence done - chờ WiFi connected...");
}
