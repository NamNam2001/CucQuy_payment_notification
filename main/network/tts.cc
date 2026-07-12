/**
 * @file tts.cc
 * @brief Text-to-Speech: HTTP stream Google Translate TTS → MP3 decode → PCM5102A.
 *
 * Architecture: STREAM real-time (KHÔNG prefetch).
 *   - Fetch MP3 chunk 4KB → decode → I2S play ngay → fetch chunk tiếp
 *   - RAM footprint: ~8KB (2 buffer scratch tái sử dụng)
 *   - Play bắt đầu ngay khi chunk đầu tiên tới (~0.5s)
 *   - i2s_channel_write() block cho tới khi DMA rảnh → phanh tự nhiên
 *     → ESP32 không đọc nhanh hơn tốc độ phát → TCP flow control lo phần còn lại
 *
 * Âm thanh output:
 *   - Google TTS trả về MP3 mono 24kHz 16-bit
 *   - I2S config 24kHz (khớp) → tốc độ chuẩn
 */
#include "tts.h"
#include "config.h"
#include "audio/audio.h"

#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_http_client.h>
#include <esp_audio_dec_default.h>
#include <esp_audio_simple_dec_default.h>
#include <esp_audio_simple_dec.h>
#include <string.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "tts";

#define HTTP_CHUNK_SIZE   4096
#define PCM_BUFFER_SIZE   4096
#define MAX_URL_LEN       768

// ---------------------------------------------------------------------------
// URL-encode (Google TTS cần UTF-8 encode + space thành '+')
// ---------------------------------------------------------------------------
static void url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 4; i++) {
        unsigned char c = (unsigned char)src[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            dst[j++] = c;
        } else if (c == ' ') {
            dst[j++] = '+';
        } else {
            dst[j++] = '%';
            dst[j++] = hex[c >> 4];
            dst[j++] = hex[c & 0x0F];
        }
    }
    dst[j] = '\0';
}

// ---------------------------------------------------------------------------
// Build URL
// ---------------------------------------------------------------------------
static void build_tts_url(const char *text, char *url, size_t url_size)
{
    char encoded[660];
    url_encode(text, encoded, sizeof(encoded));
    snprintf(url, url_size,
             "%s?ie=UTF-8&tl=%s&client=tw-ob&q=%s",
             TTS_ENDPOINT, TTS_LANGUAGE, encoded);
}

// ---------------------------------------------------------------------------
// MP3 decode context
// ---------------------------------------------------------------------------
typedef struct {
    esp_audio_simple_dec_handle_t dec;
} mp3_dec_ctx_t;

static esp_err_t mp3_dec_open(mp3_dec_ctx_t *ctx)
{
    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type      = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
        .dec_cfg       = NULL,
        .cfg_size      = 0,
        .use_frame_dec = false,
    };
    if (esp_audio_simple_dec_open(&cfg, &ctx->dec) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "esp_audio_simple_dec_open failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void mp3_dec_close(mp3_dec_ctx_t *ctx)
{
    if (ctx->dec) {
        esp_audio_simple_dec_close(ctx->dec);
        ctx->dec = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Feed MP3 buffer → decoder → PCM ra PCM5102A (dùng cho embedded MP3)
// ---------------------------------------------------------------------------
static void feed_mp3_to_speaker(mp3_dec_ctx_t *ctx, const uint8_t *mp3, size_t size)
{
    uint8_t pcm_buf[PCM_BUFFER_SIZE];
    size_t offset = 0;
    while (offset < size) {
        size_t chunk = size - offset;
        if (chunk > HTTP_CHUNK_SIZE) chunk = HTTP_CHUNK_SIZE;

        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = (uint8_t *)mp3 + offset;
        raw.len    = (uint32_t)chunk;
        raw.eos    = (offset + chunk >= size);

        while (raw.len > 0) {
            esp_audio_simple_dec_out_t out = {};
            out.buffer = pcm_buf;
            out.len    = PCM_BUFFER_SIZE;
            esp_audio_err_t ret = esp_audio_simple_dec_process(ctx->dec, &raw, &out);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) break;
            if (ret != ESP_AUDIO_ERR_OK) break;
            if (out.decoded_size > 0) {
                int samples = out.decoded_size / sizeof(int16_t);
                audio_write_pcm16((const int16_t *)pcm_buf, samples);
            }
            if (raw.consumed == 0) break;
            raw.buffer += raw.consumed;
            raw.len    -= raw.consumed;
        }
        offset += chunk;
    }
}

// ---------------------------------------------------------------------------
// STREAM + decode 1 URL → PCM5102A (real-time, RAM chỉ ~8KB)
// ---------------------------------------------------------------------------
static esp_err_t tts_stream_and_decode(const char *url)
{
    int64_t t_start = esp_timer_get_time();

    esp_http_client_config_t http_cfg = {};
    http_cfg.url = url;
    http_cfg.method = HTTP_METHOD_GET;
    http_cfg.timeout_ms = TTS_HTTP_TIMEOUT_MS;
    http_cfg.user_agent = TTS_USER_AGENT;
    // HTTP không cần TLS cert bundle (chỉ HTTPS mới cần)
    http_cfg.buffer_size = HTTP_CHUNK_SIZE;
    http_cfg.buffer_size_tx = 1024;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (client == nullptr) return ESP_FAIL;

    if (esp_http_client_open(client, 0) != ESP_OK) {
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
    esp_http_client_fetch_headers(client);
    int64_t t_tls = esp_timer_get_time();
    ESP_LOGI(TAG, "TLS + headers xong: %lld ms", (long long)((t_tls - t_start) / 1000));

    mp3_dec_ctx_t dec_ctx = {};
    if (mp3_dec_open(&dec_ctx) != ESP_OK) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    uint8_t *http_buf = (uint8_t *)malloc(HTTP_CHUNK_SIZE);
    uint8_t *pcm_buf  = (uint8_t *)malloc(PCM_BUFFER_SIZE);
    if (!http_buf || !pcm_buf) {
        free(http_buf); free(pcm_buf);
        mp3_dec_close(&dec_ctx);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    bool info_reported = false;
    int total_read = 0;
    int total_pcm_bytes = 0;

    while (1) {
        int n = esp_http_client_read(client, (char *)http_buf, HTTP_CHUNK_SIZE);
        if (n < 0) break;
        if (n == 0) break;   // EOF
        if (total_read == 0) {
            int64_t t_first = esp_timer_get_time();
            ESP_LOGI(TAG, "Chunk đầu tiên tới: %lld ms sau request",
                     (long long)((t_first - t_start) / 1000));
        }
        total_read += n;

        esp_audio_simple_dec_raw_t raw = {};
        raw.buffer = http_buf;
        raw.len    = (uint32_t)n;

        while (raw.len > 0) {
            esp_audio_simple_dec_out_t out = {};
            out.buffer = pcm_buf;
            out.len    = PCM_BUFFER_SIZE;
            esp_audio_err_t ret = esp_audio_simple_dec_process(dec_ctx.dec, &raw, &out);
            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) break;
            if (ret != ESP_AUDIO_ERR_OK) break;
            if (out.decoded_size > 0) {
                if (!info_reported) {
                    esp_audio_simple_dec_info_t info = {};
                    esp_audio_simple_dec_get_info(dec_ctx.dec, &info);
                    ESP_LOGI(TAG, "MP3 info: sr=%u ch=%u bits=%u",
                             info.sample_rate, info.channel, info.bits_per_sample);
                    info_reported = true;
                }
                int samples = out.decoded_size / sizeof(int16_t);
                audio_write_pcm16((const int16_t *)pcm_buf, samples);
                total_pcm_bytes += out.decoded_size;
            }
            if (raw.consumed == 0) break;
            raw.buffer += raw.consumed;
            raw.len    -= raw.consumed;
        }
    }

    int64_t t_end = esp_timer_get_time();
    ESP_LOGI(TAG, "TTS done: %d bytes MP3 → %d bytes PCM, tổng %lld ms",
             total_read, total_pcm_bytes, (long long)((t_end - t_start) / 1000));
    free(http_buf);
    free(pcm_buf);
    mp3_dec_close(&dec_ctx);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Chia text dài thành nhiều câu (theo . ! ?) nếu vượt TTS_MAX_TEXT_LEN
// ---------------------------------------------------------------------------
static int split_sentences(const char *text, char *out[], int max_sentences, int max_len)
{
    int count = 0;
    const char *p = text;
    char buf[TTS_MAX_TEXT_LEN + 1];
    int blen = 0;

    while (*p && count < max_sentences) {
        while (*p == ' ' || *p == '\t') p++;
        if (blen < max_len) buf[blen++] = *p;

        bool end_of_sentence = (*p == '.' || *p == '!' || *p == '?');
        bool buffer_full = (blen >= max_len - 1);
        bool last_char = (*(p + 1) == '\0');

        if (end_of_sentence || buffer_full || last_char) {
            buf[blen] = '\0';
            char *start = buf;
            while (*start == ' ') start++;
            int len = strlen(start);
            while (len > 0 && (start[len-1] == ' ' || start[len-1] == '.')) {
                start[--len] = '\0';
            }
            if (len > 0) {
                out[count] = strdup(start);
                count++;
            }
            blen = 0;
        }
        if (*p == '\0') break;
        p++;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t tts_init(void)
{
    ESP_LOGI(TAG, "Init TTS (Google Translate endpoint, lang=%s)", TTS_LANGUAGE);
    if (esp_audio_dec_register_default() != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "esp_audio_dec_register_default failed");
        return ESP_FAIL;
    }
    if (esp_audio_simple_dec_register_default() != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "esp_audio_simple_dec_register_default failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "TTS ready");
    return ESP_OK;
}

esp_err_t tts_play_embedded_mp3(const uint8_t *data, size_t size)
{
    if (data == nullptr || size == 0) return ESP_ERR_INVALID_ARG;
    mp3_dec_ctx_t dec_ctx = {};
    if (mp3_dec_open(&dec_ctx) != ESP_OK) return ESP_FAIL;
    feed_mp3_to_speaker(&dec_ctx, data, size);
    mp3_dec_close(&dec_ctx);
    return ESP_OK;
}

esp_err_t tts_speak(const char *text)
{
    if (text == nullptr || text[0] == '\0') return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "speak: \"%s\" (free heap=%u)",
             text, (unsigned)esp_get_free_heap_size());

    esp_err_t final_ret = ESP_OK;

    if (strlen(text) <= TTS_MAX_TEXT_LEN) {
        char url[MAX_URL_LEN];
        build_tts_url(text, url, sizeof(url));
        if (tts_stream_and_decode(url) != ESP_OK) final_ret = ESP_FAIL;
    } else {
        char *sentences[8];
        int n = split_sentences(text, sentences, 8, TTS_MAX_TEXT_LEN);
        for (int i = 0; i < n; i++) {
            char url[MAX_URL_LEN];
            build_tts_url(sentences[i], url, sizeof(url));
            if (tts_stream_and_decode(url) != ESP_OK) final_ret = ESP_FAIL;
            free(sentences[i]);
        }
    }
    return final_ret;
}
