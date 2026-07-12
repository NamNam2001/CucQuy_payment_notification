/**
 * @file main.cc
 * @brief Entry point - NamPOS Payment Notification.
 *
 * Luồng khởi tạo:
 *   1. display_init()        - LCD ST7735 + LVGL (màn Home "Hello NamPOS")
 *   2. audio_init()          - I2S cho PCM5102A
 *   3. wifi_init()           - kết nối WiFi
 *   4. mqtt_init()           - kết nối MQTT broker
 *   5. notify_app_init()     - subscribe order/create + order/paid
 *
 * Sau đó app_main kết thúc - mọi việc drive bởi MQTT event.
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
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== NamPOS Payment Notification ===");
    ESP_LOGI(TAG, "ESP32-WROOM 4MB + ST7735 128x160 + PCM5102A");

    // ---- 1. Display (boot vào màn Home "Hello NamPOS") ----
    ESP_ERROR_CHECK(display_init());

    // ---- 2. Audio ----
    ESP_ERROR_CHECK(audio_init());
    ESP_ERROR_CHECK(tts_init());

    // ---- 3. WiFi ----
    ESP_LOGI(TAG, "Connecting WiFi '%s' ...", WIFI_SSID);
    if (wifi_init() != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect FAILED - reboot trong 10s");
        vTaskDelay(pdMS_TO_TICKS(10000));
        esp_restart();
    }

    // ---- 4. MQTT (start client, tự reconnect nền) ----
    mqtt_init();

    // ---- 5. Notify app (subscribe 2 topic) ----
    notify_app_init();

    // ---- 6. Sẵn sàng ----
    ESP_LOGI(TAG, "System ready");
    ESP_LOGI(TAG, "  topic create: %s", MQTT_TOPIC_ORDER_CREATE);
    ESP_LOGI(TAG, "  topic paid:   %s", MQTT_TOPIC_ORDER_PAID);

    // Boot beep xác nhận
    audio_play_beep(660, 100);
    vTaskDelay(pdMS_TO_TICKS(80));
    audio_play_beep(880, 150);
}
