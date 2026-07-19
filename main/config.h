/**
 * @file config.h
 * @brief Pin definitions + cấu hình phần cứng cho board CucQuy Payment Notification.
 *
 * Tương thích pinout với board `bread-compact-esp32-lcd` của xiaozhi-esp32
 * (target ESP32-WROOM 4MB flash).
 *
 * Phần cứng:
 *   - ESP32-WROOM-32 (4MB flash, không PSRAM)
 *   - LCD ST7735 128x160 (SPI, driver bằng esp_lcd_new_panel_st7789 của IDF)
 *   - PCM5102A (I2S DAC, không cần MCLK, không có I2C control)
 *
 * Lưu ý về GPIO trên ESP32-WROOM:
 *   - GPIO 6-11:   NỐI VỚI FLASH - KHÔNG ĐƯỢC DÙNG
 *   - GPIO 12:     MTDI - tránh dùng làm input (chiến STRAP)
 *   - GPIO 0,2,15: boot strap - cẩn thận khi dùng
 */
#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include "esp_lcd_types.h"

// =============================================================================
// WIZI / Network config (đổi theo môi trường)
// =============================================================================
// WiFi được cấu hình qua captive portal (như xiaozhi-esp32)
// Lần đầu boot: ESP32 phát AP "NamPOS-XXXX" -> user vào 192.168.4.1 nhập WiFi
// SSID/password lưu trong NVS (namespace "wifi"), KHÔNG hardcode
#define WIFI_AP_PREFIX              "NamPOS"
#define WIFI_CONNECT_TIMEOUT_SEC    60    // sau 60s không kết nối được -> vào config AP

// HiveMQ public test broker - ho tro user/pass tuy y (de test)
//   - TCP plaintext: mqtt://broker.hivemq.com:1883 (user/pass optional)
//   - TLS:           mqtts://broker.hivemq.com:8883 (user/pass optional)
#define MQTT_BROKER_URI     "wss://mqtt.cucquy.site:443/mqtt"
#define MQTT_USERNAME       "cucquy"
#define MQTT_PASSWORD       "REPLACE_WITH_MQTT_PASSWORD"
#define MQTT_CLIENT_ID      "cucquy-payment-001"

// 2 topic cho workflow pos: create (show QR) + paid (bao loa)
#define MQTT_TOPIC_ORDER_CREATE  "cucquy/esp_01/order/create"
#define MQTT_TOPIC_ORDER_PAID    "cucquy/esp_01/order/paid"
#define MQTT_TOPIC_ORDER_CANCEL  "cucquy/esp_01/order/cancel"

// Màn hình chính - chữ hiển thị giữa màn hình Home
#define DISPLAY_HOME_TEXT        "Hello NamPOS"

// =============================================================================
// TTS - Text To Speech (Google Translate endpoint)
// =============================================================================
// Dùng HTTP (không TLS) để tránh 1.9s TLS handshake.
// Nội dung chỉ là audio TTS công khai, không cần mã hóa. Google trả 200 OK qua HTTP.
#define TTS_ENDPOINT        "http://translate.google.com/translate_tts"
#define TTS_LANGUAGE        "vi"                          // tiếng Việt
#define TTS_USER_AGENT      "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"
#define TTS_HTTP_TIMEOUT_MS 8000                          // timeout 8s
#define TTS_MAX_TEXT_LEN    180                           // Google giới hạn ~200 ký tự/request

// =============================================================================
// DISPLAY - ST7735 128x160 (SPI)
// Pinout lấy từ bread-compact-esp32-lcd/config.h
// =============================================================================
#define DISPLAY_CS_PIN        GPIO_NUM_22
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_23   // PWM backlight
#define DISPLAY_MOSI_PIN      GPIO_NUM_4
#define DISPLAY_CLK_PIN       GPIO_NUM_15
#define DISPLAY_DC_PIN        GPIO_NUM_21
#define DISPLAY_RST_PIN       GPIO_NUM_18

// Cấu hình panel ST7735 128x160 (đồng bộ với xiaozhi CONFIG_LCD_ST7735_128X160)
#define LCD_TYPE_ST7789_SERIAL              // ST7735 register-compatible với ST7789
#define DISPLAY_WIDTH                       128
#define DISPLAY_HEIGHT                      160
#define DISPLAY_MIRROR_X                    true
#define DISPLAY_MIRROR_Y                    true
#define DISPLAY_SWAP_XY                     false
#define DISPLAY_INVERT_COLOR                false
#define DISPLAY_RGB_ORDER                   LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X                    0
#define DISPLAY_OFFSET_Y                    0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT     false
#define DISPLAY_SPI_MODE                    0
#define DISPLAY_SPI_HOST                    SPI3_HOST
#define DISPLAY_SPI_PCLK_HZ                 (40 * 1000 * 1000)   // 40 MHz

// =============================================================================
// AUDIO - PCM5102A (I2S DAC, stereo, không cần MCLK)
//
// Pin IDENTICAL với xiaozhi bread-compact-esp32-lcd/config.h (mode SIMPLEX).
// Pin speaker tách biệt hoàn toàn với pin display -> KHÔNG conflict.
//
// PCM5102A wiring:
//   - BCK  -> AUDIO_I2S_SPK_GPIO_BCLK (GPIO14)
//   - LRCK/WS -> AUDIO_I2S_SPK_GPIO_LRCK (GPIO27)
//   - DIN  -> AUDIO_I2S_SPK_GPIO_DOUT (GPIO33)   (DOUT của ESP32 = DIN của DAC)
//   - SCK  -> GND (PCM5102A tự sinh master clock từ BCK)
//   - GND, VIN(3.3V) theo datasheet
// =============================================================================
#define AUDIO_I2S_METHOD_SIMPLEX                      // dùng Simplex (đúng với bread-compact-esp32-lcd)

// QUAN TRỌNG: Google TTS trả MP3 24kHz mono. Phải set I2S = 24000 để tốc độ
// đọc bình thường. Nếu set 44100 → giọng bị chạy nhanh 1.84x (chipmunk).
// (PCM5102A support 8-384kHz nên 24kHz hoàn toàn OK)
#define AUDIO_OUTPUT_SAMPLE_RATE            24000

// Speaker (PCM5102A) - đúng nguyên gốc xiaozhi
#define AUDIO_I2S_SPK_GPIO_DOUT             GPIO_NUM_33
#define AUDIO_I2S_SPK_GPIO_BCLK             GPIO_NUM_14
#define AUDIO_I2S_SPK_GPIO_LRCK             GPIO_NUM_27

// Mic (INMP441/...) - giữ cho đầy đủ, dự án này KHÔNG dùng mic (chỉ playback)
// #define AUDIO_I2S_MIC_GPIO_WS               GPIO_NUM_25
// #define AUDIO_I2S_MIC_GPIO_SCK              GPIO_NUM_26
// #define AUDIO_I2S_MIC_GPIO_DIN              GPIO_NUM_32

// DMA config (tham khảo xiaozhi audio_codec.h)
#define AUDIO_CODEC_DMA_DESC_NUM            6
#define AUDIO_CODEC_DMA_FRAME_NUM           240

// Volume 0-100 (applied in software - PCM5102A không có volume control qua I2S)
#define AUDIO_DEFAULT_VOLUME                70

// =============================================================================
// BUTTONS / LED
// =============================================================================
#define BOOT_BUTTON_GPIO                    GPIO_NUM_0    // nút FLASH trên module
#define BUILTIN_LED_GPIO                    GPIO_NUM_2

#endif // _BOARD_CONFIG_H_
