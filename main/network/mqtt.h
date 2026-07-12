/**
 * @file mqtt.h
 * @brief MQTT client - dùng trực tiếp esp-mqtt built-in ESP-IDF.
 *
 * KHÔNG dùng 78/esp-ml307 (đó là wrapper cho cả 4G modem + WiFi).
 * Mình chỉ cần WiFi -> gọi esp_mqtt_client_* trực tiếp.
 *
 * Tham chiếu pattern: xiaozhi managed_components/78__esp-ml307/src/esp/esp_mqtt.cc
 */
#pragma once

#include <esp_err.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo MQTT client và kết nối broker (URI ở config.h).
 *        Block cho tới khi connected hoặc timeout 10s.
 * @return ESP_OK nếu connected.
 */
esp_err_t mqtt_init(void);

/**
 * @brief Subscribe một topic. Gọi sau khi mqtt_init thành công.
 */
esp_err_t mqtt_subscribe(const char *topic, int qos);

/**
 * @brief Publish message.
 */
esp_err_t mqtt_publish(const char *topic, const char *payload, int qos);

/**
 * @brief Callback khi nhận được message từ topic đã subscribe.
 *        Chạy trong MQTT task - phải trả về nhanh, defer work sang task khác.
 *
 * @param topic      Topic string (null-terminated).
 * @param payload    Payload buffer (KHÔNG null-terminated, dùng payload_len).
 * @param payload_len Số byte payload.
 */
typedef void (*mqtt_message_cb_t)(const char *topic, const char *payload, int payload_len);

/**
 * @brief Callback khi MQTT (re)connect thành công.
 *        Dùng để re-subscribe hoặc refresh state. esp-mqtt tự reconnect nên
 *        callback này được gọi mỗi lần kết nối lại.
 */
typedef void (*mqtt_connected_cb_t)(void);

void mqtt_on_message(mqtt_message_cb_t cb);
void mqtt_on_connected(mqtt_connected_cb_t cb);

bool mqtt_is_connected(void);

#ifdef __cplusplus
}
#endif
