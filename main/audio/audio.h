/**
 * @file audio.h
 * @brief I2S output cho PCM5102A DAC.
 *
 * Tham chiếu: xiaozhi-esp32/main/audio/codecs/no_audio_codec.cc
 * (lớp NoAudioCodecSimplex - I2S TX cho DAC không có I2C control).
 *
 * PCM5102A:
 *   - Không cần MCLK (tự sinh từ BCK)
 *   - Không có I2C/SPI control -> dùng software volume
 *   - Stereo, 32-bit slot, hỗ trợ 8-384 kHz
 *
 * Trước mắt module này chỉ lo I2S output. Việc decode WAV/OGG sẽ thêm sau
 * (khi thêm espressif/esp_audio_codec).
 */
#pragma once

#include <esp_err.h>
#include <stdint.h>
#include <stddef.h>

/**
 * @brief Khởi tạo I2S TX cho PCM5102A.
 *
 * Cấu hình (theo no_audio_codec.cc:79-146):
 *   - I2S_NUM_0, role master
 *   - 32-bit data slot, MONO hoặc STEREO
 *   - MCLK unused, BCLK + LRCK + DOUT
 *   - DMA: 6 descriptor x 240 frame
 *
 * @return ESP_OK hay lỗi.
 */
esp_err_t audio_init(void);

/**
 * @brief Enable/disable I2S TX channel.
 *        Gọi enable=true trước khi ghi, false để tắt(loa im).
 */
esp_err_t audio_enable_output(bool enable);

/**
 * @brief Ghi sample 16-bit PCM ra PCM5102A.
 *
 * Hàm này tự mở rộng 16->32 bit và apply volume (theo no_audio_codec.cc:218-239).
 * Gọi sau khi audio_enable_output(true).
 *
 * @param samples  Buffer int16_t PCM mono.
 * @param count    Số lượng sample (mỗi sample = 1 int16_t).
 * @return Số sample thực tế đã ghi.
 */
int audio_write_pcm16(const int16_t *samples, size_t count);

/**
 * @brief Set volume 0-100 (software, apply trong audio_write_pcm16).
 */
void audio_set_volume(int volume_pct);

/**
 * @brief Phát một tone beep đơn giản (test loa). Tần số A4 = 440Hz.
 */
esp_err_t audio_play_beep(int frequency_hz, int duration_ms);
