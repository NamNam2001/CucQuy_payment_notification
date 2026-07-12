/**
 * @file wifi.h
 * @brief WiFi Manager wrapper - dùng 78/esp-wifi-connect (captive portal).
 *
 * Flow:
 *   wifi_init() -> check NVS có SSID?
 *     ├── CÓ    -> StartStation -> (connected callback) -> start MQTT
 *     └── KHÔNG -> StartConfigAp (captive portal "NamPOS-XXXX")
 *
 *   Nút GPIO0 nhấn giữ 2s -> wifi_enter_config_mode() -> mở lại AP
 */
#pragma once

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo WiFi manager + quyết định station hay config AP.
 *        Tự init NVS, netif, event loop, wifi driver.
 */
esp_err_t wifi_init(void);

/**
 * @brief Vào chế độ config AP (captive portal) - gọi từ nút bấm GPIO0.
 *        Tạo task riêng để tránh race condition với event handlers.
 */
void wifi_enter_config_mode(void);

/**
 * @brief Kiểm tra WiFi đã connect + có IP.
 */
bool wifi_is_connected(void);

/**
 * @brief Callback khi WiFi connected (IP_EVENT_STA_GOT_IP).
 *        Nơi thích hợp để start MQTT.
 */
typedef void (*wifi_connected_cb_t)(void);
void wifi_on_connected(wifi_connected_cb_t cb);

/**
 * @brief Callback khi WiFi disconnect.
 */
typedef void (*wifi_disconnected_cb_t)(void);
void wifi_on_disconnected(wifi_disconnected_cb_t cb);

#ifdef __cplusplus
}
#endif
