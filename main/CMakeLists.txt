# main/CMakeLists.txt - CucQuy Payment Notification
#
# Project: ESP32-WROOM 4MB + ST7735 128x160 + PCM5102A
# Tham chiếu: xiaozhi-esp32 board bread-compact-esp32-lcd

set(SOURCES
    "main.cc"
    "display/display.cc"
    "audio/audio.cc"
    "network/wifi.cc"
    "network/mqtt.cc"
    "network/tts.cc"
    "app/notify_app.cc"
)

set(INCLUDE_DIRS
    "."
    "display"
    "audio"
    "network"
    "app"
)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS ${INCLUDE_DIRS}
                    EMBED_FILES
                        "assets/tinh_tinh.mp3"
                    PRIV_REQUIRES
                        # ESP-IDF built-in components
                        nvs_flash
                        esp_event
                        mqtt
                        esp_driver_gpio
                        esp_driver_spi
                        esp_driver_i2s
                        json
                        esp_http_client
                        # Managed components
                        # (esp_lvgl_port + lvgl + esp_audio_codec + 78/esp-wifi-connect tự kéo qua dependencies)
                    )
