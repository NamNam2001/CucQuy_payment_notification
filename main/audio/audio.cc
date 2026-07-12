/**
 * @file audio.cc
 * @brief I2S TX cho PCM5102A DAC (ESP32).
 *
 * Tham chiếu: xiaozhi-esp32/main/audio/codecs/no_audio_codec.cc
 *   - I2S driver mới (driver/i2s_std.h, channel-based API)
 *   - i2s_new_channel + i2s_channel_init_std_mode
 *   - MCLK unused, BCLK + LRCK + DOUT
 *   - Write(): mở rộng int16 -> int32 + apply volume (no_audio_codec.cc:218-239)
 */
#include "audio.h"
#include "config.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "audio";

static i2s_chan_handle_t s_tx_handle = nullptr;
static int               s_volume    = AUDIO_DEFAULT_VOLUME;   // 0..100

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t audio_init(void)
{
    ESP_LOGI(TAG, "Init I2S TX for PCM5102A (%dHz)", AUDIO_OUTPUT_SAMPLE_RATE);

    // ---- 1. Channel create (no_audio_codec.cc:85-94) ----
    i2s_chan_config_t chan_cfg = {};
    chan_cfg.id                   = I2S_NUM_0;
    chan_cfg.role                 = I2S_ROLE_MASTER;
    chan_cfg.dma_desc_num         = AUDIO_CODEC_DMA_DESC_NUM;
    chan_cfg.dma_frame_num        = AUDIO_CODEC_DMA_FRAME_NUM;
    chan_cfg.auto_clear_after_cb  = true;
    chan_cfg.auto_clear_before_cb = false;
    chan_cfg.intr_priority        = 0;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_handle, nullptr));

    // ---- 2. Std mode config (CHÍNH XÁC theo no_audio_codec.cc:96-134) ----
    // PCM5102A: mono, 32-bit slot, MCLK unused (SCK của PCM5102A kéo GND).
    // QUAN TRỌNG: dùng MONO + SLOT_LEFT (giống xiaozhi). Đã test chạy được
    // với PCM5102A. Đừng đổi sang STEREO/SLOT_BOTH - sẽ sai data rate.
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg.sample_rate_hz  = (uint32_t)AUDIO_OUTPUT_SAMPLE_RATE;
    std_cfg.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
    std_cfg.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_256;

    std_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
    std_cfg.slot_cfg.slot_mode      = I2S_SLOT_MODE_MONO;
    std_cfg.slot_cfg.slot_mask      = I2S_STD_SLOT_LEFT;
    std_cfg.slot_cfg.ws_width       = I2S_DATA_BIT_WIDTH_32BIT;
    std_cfg.slot_cfg.ws_pol         = false;
    std_cfg.slot_cfg.bit_shift      = true;

    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;   // PCM5102A không cần MCLK
    std_cfg.gpio_cfg.bclk = AUDIO_I2S_SPK_GPIO_BCLK;
    std_cfg.gpio_cfg.ws   = AUDIO_I2S_SPK_GPIO_LRCK;
    std_cfg.gpio_cfg.dout = AUDIO_I2S_SPK_GPIO_DOUT;
    std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_handle, &std_cfg));

    ESP_LOGI(TAG, "I2S ready (BCLK=%d LRCK=%d DOUT=%d)",
             AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT);
    return ESP_OK;
}

// Track state để audio_enable_output idempotent (KHÔNG crash khi enable 2 lần)
static bool s_i2s_enabled = false;

esp_err_t audio_enable_output(bool enable)
{
    if (s_tx_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    // Idempotent: bỏ qua nếu state không đổi
    if (enable && s_i2s_enabled)  return ESP_OK;
    if (!enable && !s_i2s_enabled) return ESP_OK;

    esp_err_t err;
    if (enable) {
        err = i2s_channel_enable(s_tx_handle);
        if (err == ESP_ERR_INVALID_STATE) {
            // Đã enable rồi - OK
            err = ESP_OK;
        }
    } else {
        err = i2s_channel_disable(s_tx_handle);
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;
        }
    }
    if (err == ESP_OK) {
        s_i2s_enabled = enable;
    }
    return err;
}

// Theo no_audio_codec.cc:218-239 (1:1 - mỗi int16 → 1 int32, MONO)
int audio_write_pcm16(const int16_t *samples, size_t count)
{
    if (s_tx_handle == nullptr || samples == nullptr || count == 0) {
        return 0;
    }

    // Mở rộng int16 -> int32 + apply volume (1:1, không duplicate)
    int32_t volume_factor = (int32_t)(pow((double)s_volume / 100.0, 2) * 65536);
    std::vector<int32_t> buffer(count);
    for (size_t i = 0; i < count; i++) {
        int64_t scaled = (int64_t)samples[i] * volume_factor;
        if (scaled > INT32_MAX) scaled = INT32_MAX;
        if (scaled < INT32_MIN) scaled = INT32_MIN;
        buffer[i] = (int32_t)scaled;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(s_tx_handle, buffer.data(),
                                      buffer.size() * sizeof(int32_t),
                                      &bytes_written, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "i2s_channel_write: %s", esp_err_to_name(err));
        return 0;
    }
    return (int)(bytes_written / sizeof(int32_t));
}

void audio_set_volume(int volume_pct)
{
    if (volume_pct < 0)   volume_pct = 0;
    if (volume_pct > 100) volume_pct = 100;
    s_volume = volume_pct;
}

// ---------------------------------------------------------------------------
// Beep test - phát tone sine ra PCM5102A
// ---------------------------------------------------------------------------
esp_err_t audio_play_beep(int frequency_hz, int duration_ms)
{
    if (s_tx_handle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    // Không dùng ESP_ERROR_CHECK - tránh crash nếu I2S đang enable
    audio_enable_output(true);

    const int sample_rate = AUDIO_OUTPUT_SAMPLE_RATE;
    const int total_samples = sample_rate * duration_ms / 1000;
    const int block = 1024;   // mỗi block 1024 samples

    int16_t *pcm = (int16_t *)malloc(block * sizeof(int16_t));
    if (pcm == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    int written = 0;
    while (written < total_samples) {
        int n = block;
        if (written + n > total_samples) {
            n = total_samples - written;
        }
        for (int i = 0; i < n; i++) {
            // sine wave amplitude 0.8 * INT16_MAX
            double t = (double)(written + i) / sample_rate;
            pcm[i] = (int16_t)(0.8 * 32767.0 * sin(2.0 * M_PI * frequency_hz * t));
        }
        audio_write_pcm16(pcm, n);
        written += n;
    }

    free(pcm);
    vTaskDelay(pdMS_TO_TICKS(20));   // chờ DMA đẩy nốt
    ESP_ERROR_CHECK(audio_enable_output(false));
    return ESP_OK;
}
