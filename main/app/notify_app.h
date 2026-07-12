/**
 * @file notify_app.h
 * @brief Logic chính: nhận MQTT message -> phát beep thông báo + update title.
 */
#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo notify app (register MQTT callback, set QR default, ...).
 *        Gọi sau khi display + audio + mqtt đã init xong.
 */
esp_err_t notify_app_init(void);

#ifdef __cplusplus
}
#endif
