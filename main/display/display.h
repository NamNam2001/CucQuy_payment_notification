/**
 * @file display.h
 * @brief LCD ST7735 128x160 + LVGL - 2 màn hình: Home + QR payment.
 *
 * Màn Home:  "Hello NamPOS" ở giữa (chờ đơn)
 * Màn QR:    QR 100x100 ở trên + số tiền ở dưới
 */
#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo SPI bus, LCD panel ST7735, backlight, và LVGL.
 *        Màn hình bắt đầu ở màn Home ("Hello NamPOS").
 */
esp_err_t display_init(void);

/**
 * @brief Chuyển sang màn QR - hiển thị EMV payload + số tiền + mã đơn.
 *
 * @param emv_payload  Chuỗi EMV CoQR (do server gen, vd "00020101...")
 * @param amount_vnd   Số tiền VND (vd 50000) - hiển thị dưới QR
 * @param order_id     Mã đơn hàng (vd "DH001") - tự xuống hàng nếu dài
 */
esp_err_t display_show_qr(const char *emv_payload, int amount_vnd, const char *order_id);

/**
 * @brief Chuyển về màn Home ("Hello NamPOS").
 */
esp_err_t display_show_home(void);

/**
 * @brief Hiển thị màn hình config mode:
 *        "Vao WiFi: NamPOS-XXXX" + "Cau hinh: 192.168.4.1"
 */
esp_err_t display_show_config_mode(const char *ap_name);

/**
 * @brief Cập nhật WiFi icon ở góc phải màn Home.
 *        connected=true: icon đầy (có sóng). false: icon mờ/dấu X.
 */
esp_err_t display_set_wifi_status(bool connected);

/** Cập nhật dòng tiêu đề phía trên (optional). */
esp_err_t display_set_title(const char *text);

/** Khóa/unlock LVGL (thread-safe khi update từ task khác). */
void display_lock(void);
void display_unlock(void);

#ifdef __cplusplus
}
#endif
