/**
 * @file wifi.h
 * @brief WiFi Station - dùng trực tiếp esp_wifi của ESP-IDF.
 *
 * Không phụ thuộc wrapper nào. Cung cấp callback khi WiFi connected/disconnected.
 */
#pragma once

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo NVS + netif + WiFi ở chế độ STA, bắt đầu kết nối.
 *
 * @return ESP_OK hay lỗi.
 */
esp_err_t wifi_init(void);

/**
 * @brief Đăng ký callback khi WiFi拿到IP (connected) / mất kết nối.
 *        Callback chạy trong event task của ESP-IDF - phải trả về nhanh.
 */
typedef void (*wifi_connected_cb_t)(void);
typedef void (*wifi_disconnected_cb_t)(void);

void wifi_on_connected(wifi_connected_cb_t cb);
void wifi_on_disconnected(wifi_disconnected_cb_t cb);

/**
 * @brief Kiểm tra WiFi đã kết nối + có IP chưa.
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif
