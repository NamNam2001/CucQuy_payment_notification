/**
 * @file tts.h
 * @brief Text-to-Speech dùng Google Translate endpoint + MP3 decode + I2S output.
 *
 * Luồng:
 *   tts_speak("text") →
 *     1. URL-encode text
 *     2. esp_http_client GET endpoint (tl=vi)
 *     3. Stream MP3 về từng chunk 4KB
 *     4. Decode MP3 → PCM 16-bit qua esp_audio_codec (simple dec)
 *     5. Push PCM ra PCM5102A qua audio_write_pcm16()
 *
 * Stream real-time: fetch + decode + play CÙNG LÚC. RAM chỉ ~8KB.
 * Play bắt đầu ngay khi chunk đầu tiên tới (~0.5s sau request).
 */
#pragma once

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Khởi tạo MP3 decoder (chạy 1 lần lúc boot).
 */
esp_err_t tts_init(void);

/**
 * @brief Đọc text tiếng Việt thành tiếng nói qua PCM5102A.
 *        Stream real-time (fetch + decode + play cùng lúc, RAM chỉ ~8KB).
 */
esp_err_t tts_speak(const char *text);

/**
 * @brief Play một MP3 embedded trong firmware (từ EMBED_FILES trong CMakeLists).
 */
esp_err_t tts_play_embedded_mp3(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif
