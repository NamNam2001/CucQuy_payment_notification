/**
 * @file display.cc
 * @brief LCD ST7735 128x160 + LVGL - 2 màn hình Home + QR.
 *
 * Tham chiếu: xiaozhi bread-compact-esp32-lcd + lcd_display.cc
 */
#include "display.h"
#include "config.h"

#include <esp_log.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>
#include <driver/ledc.h>
#include <esp_lvgl_port.h>
#include <lvgl.h>

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static esp_lcd_panel_handle_t    s_panel    = nullptr;
static lv_display_t             *s_disp     = nullptr;

// 2 màn hình (screen objects)
static lv_obj_t *s_scr_home = nullptr;
static lv_obj_t *s_scr_qr   = nullptr;

// Widget màn Home
static lv_obj_t *s_home_title  = nullptr;
static lv_obj_t *s_home_wifi   = nullptr;   // icon WiFi góc phải

// Widget màn Config Mode
static lv_obj_t *s_scr_config  = nullptr;

// Widget màn QR
static lv_obj_t *s_qr_img        = nullptr;
static lv_obj_t *s_qr_amount     = nullptr;
static lv_obj_t *s_qr_order      = nullptr;
static lv_obj_t *s_qr_container  = nullptr;

// Track state
static bool s_i2s_enabled = false;
extern "C" bool display_is_i2s_enabled(void) { return s_i2s_enabled; }
extern "C" void display_set_i2s_enabled(bool v) { s_i2s_enabled = v; }

// ---------------------------------------------------------------------------
// Backlight PWM
// ---------------------------------------------------------------------------
static esp_err_t init_backlight(void)
{
    if (DISPLAY_BACKLIGHT_PIN == GPIO_NUM_NC) return ESP_OK;
    ledc_timer_config_t timer_cfg = {};
    timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
    timer_cfg.timer_num       = LEDC_TIMER_0;
    timer_cfg.duty_resolution = LEDC_TIMER_8_BIT;
    timer_cfg.freq_hz         = 5000;
    timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {};
    ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
    ch_cfg.channel    = LEDC_CHANNEL_0;
    ch_cfg.timer_sel  = LEDC_TIMER_0;
    ch_cfg.intr_type  = LEDC_INTR_DISABLE;
    ch_cfg.gpio_num   = DISPLAY_BACKLIGHT_PIN;
    ch_cfg.duty       = DISPLAY_BACKLIGHT_OUTPUT_INVERT ? 0 : 255;
    ch_cfg.hpoint     = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// SPI bus + LCD panel IO + ST7735 panel (qua driver ST7789)
// ---------------------------------------------------------------------------
static esp_err_t init_spi_lcd(void)
{
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num     = DISPLAY_MOSI_PIN;
    buscfg.miso_io_num     = GPIO_NUM_NC;
    buscfg.sclk_io_num     = DISPLAY_CLK_PIN;
    buscfg.quadwp_io_num   = GPIO_NUM_NC;
    buscfg.quadhd_io_num   = GPIO_NUM_NC;
    buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num    = DISPLAY_CS_PIN;
    io_config.dc_gpio_num    = DISPLAY_DC_PIN;
    io_config.spi_mode       = DISPLAY_SPI_MODE;
    io_config.pclk_hz        = DISPLAY_SPI_PCLK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits   = 8;
    io_config.lcd_param_bits = 8;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(DISPLAY_SPI_HOST, &io_config, &s_panel_io));

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = DISPLAY_RST_PIN;
    panel_config.rgb_ele_order  = DISPLAY_RGB_ORDER;
    panel_config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_panel_io, &panel_config, &s_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
    // BẮT BUỘC: bật display (st7789 driver không tự gửi DISPON)
    {
        esp_err_t on_err = esp_lcd_panel_disp_on_off(s_panel, true);
        if (on_err != ESP_ERR_NOT_SUPPORTED) ESP_ERROR_CHECK(on_err);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_panel, DISPLAY_INVERT_COLOR));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(s_panel, DISPLAY_SWAP_XY));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(s_panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y));
    if (DISPLAY_OFFSET_X || DISPLAY_OFFSET_Y) {
        ESP_ERROR_CHECK(esp_lcd_panel_set_gap(s_panel, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y));
    }
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// LVGL port
// ---------------------------------------------------------------------------
static esp_err_t init_lvgl(void)
{
    lv_init();   // bắt buộc trước lvgl_port_init

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle    = s_panel_io;
    disp_cfg.panel_handle = s_panel;
    disp_cfg.buffer_size  = DISPLAY_WIDTH * 20;
    disp_cfg.double_buffer = false;
    disp_cfg.hres         = DISPLAY_WIDTH;
    disp_cfg.vres         = DISPLAY_HEIGHT;
    disp_cfg.monochrome   = false;
    disp_cfg.rotation     = {
        .swap_xy  = DISPLAY_SWAP_XY,
        .mirror_x = DISPLAY_MIRROR_X,
        .mirror_y = DISPLAY_MIRROR_Y,
    };
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.flags.buff_dma    = 1;
    disp_cfg.flags.buff_spiram = 0;
    disp_cfg.flags.sw_rotate   = 0;
    disp_cfg.flags.swap_bytes  = 1;
    disp_cfg.flags.full_refresh = 0;
    disp_cfg.flags.direct_mode  = 0;

    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_disp == nullptr) return ESP_FAIL;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Build 2 màn hình
// ---------------------------------------------------------------------------
static void build_home_screen(void)
{
    s_scr_home = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr_home, lv_color_white(), 0);
    lv_obj_remove_flag(s_scr_home, LV_OBJ_FLAG_SCROLLABLE);

    // Chữ "Hello NamPOS" ở giữa
    s_home_title = lv_label_create(s_scr_home);
    lv_label_set_text(s_home_title, DISPLAY_HOME_TEXT);
    lv_obj_set_style_text_color(s_home_title, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_text_font(s_home_title, &lv_font_montserrat_16, 0);
    lv_obj_align(s_home_title, LV_ALIGN_CENTER, 0, 0);

    // WiFi icon góc trên-phải (dùng ký tự Unicode)
    s_home_wifi = lv_label_create(s_scr_home);
    lv_label_set_text(s_home_wifi, LV_SYMBOL_WIFI);   // icon WiFi built-in LVGL
    lv_obj_set_style_text_color(s_home_wifi, lv_color_hex(0x999999), 0);   // xám = disconnect
    lv_obj_align(s_home_wifi, LV_ALIGN_TOP_RIGHT, -4, 2);
}

static void build_config_screen(void)
{
    s_scr_config = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr_config, lv_color_hex(0xFFEB3B), 0);   // nền vàng nổi bật
    lv_obj_remove_flag(s_scr_config, LV_OBJ_FLAG_SCROLLABLE);

    // Tiêu đề
    lv_obj_t *title = lv_label_create(s_scr_config);
    lv_label_set_text(title, "WIFI SETUP");
    lv_obj_set_style_text_color(title, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    // Bước 1
    lv_obj_t *line1 = lv_label_create(s_scr_config);
    lv_label_set_text(line1, "1. Connect to WiFi:");
    lv_obj_set_style_text_color(line1, lv_color_black(), 0);
    lv_obj_align(line1, LV_ALIGN_TOP_MID, 0, 38);

    lv_obj_t *apname = lv_label_create(s_scr_config);
    lv_label_set_text(apname, "NamPOS-XXXX");
    lv_obj_set_style_text_color(apname, lv_color_hex(0x0000CC), 0);
    lv_obj_set_style_text_font(apname, &lv_font_montserrat_16, 0);
    lv_obj_align(apname, LV_ALIGN_TOP_MID, 0, 58);

    // Bước 2
    lv_obj_t *line2 = lv_label_create(s_scr_config);
    lv_label_set_text(line2, "2. Open browser:");
    lv_obj_set_style_text_color(line2, lv_color_black(), 0);
    lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, 88);

    lv_obj_t *url = lv_label_create(s_scr_config);
    lv_label_set_text(url, "192.168.4.1");
    lv_obj_set_style_text_color(url, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_text_font(url, &lv_font_montserrat_16, 0);
    lv_obj_align(url, LV_ALIGN_TOP_MID, 0, 108);

    // Note
    lv_obj_t *note = lv_label_create(s_scr_config);
    lv_label_set_text(note, "to configure WiFi");
    lv_obj_set_style_text_color(note, lv_color_hex(0x555555), 0);
    lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 134);
}

static void build_qr_screen(void)
{
    s_scr_qr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_scr_qr, lv_color_white(), 0);
    lv_obj_remove_flag(s_scr_qr, LV_OBJ_FLAG_SCROLLABLE);

    // Container chứa 3 thành phần, canh giữa màn hình, flex column
    s_qr_container = lv_obj_create(s_scr_qr);
    lv_obj_remove_style_all(s_qr_container);   // bỏ border/padding mặc định
    lv_obj_set_size(s_qr_container, 120, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_qr_container, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(s_qr_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_layout(s_qr_container, LV_LAYOUT_FLEX, 0);
    lv_obj_set_flex_flow(s_qr_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_qr_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_align(s_qr_container, LV_ALIGN_CENTER, 0, 0);

    // 1. QR 100x100 (trên cùng)
    s_qr_img = lv_qrcode_create(s_qr_container);
    lv_qrcode_set_size(s_qr_img, 100);
    lv_qrcode_set_dark_color(s_qr_img, lv_color_black());
    lv_qrcode_set_light_color(s_qr_img, lv_color_white());
    lv_qrcode_set_data(s_qr_img, "https://nampos.example.com");

    // 2. Số tiền (giữa - đỏ, font lớn)
    s_qr_amount = lv_label_create(s_qr_container);
    lv_label_set_text(s_qr_amount, "0 VND");
    lv_obj_set_style_text_color(s_qr_amount, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_text_font(s_qr_amount, &lv_font_montserrat_16, 0);
    lv_obj_set_style_pad_top(s_qr_amount, 6, 0);

    // 3. Mã đơn hàng (dưới - xám, font nhỏ, tự xuống hàng nếu dài)
    s_qr_order = lv_label_create(s_qr_container);
    lv_label_set_text(s_qr_order, "");
    lv_obj_set_style_text_color(s_qr_order, lv_color_hex(0x333333), 0);
    lv_obj_set_width(s_qr_order, 116);   // giới hạn rộng → tự wrap
    lv_label_set_long_mode(s_qr_order, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_qr_order, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_qr_order, 2, 0);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
esp_err_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing ST7735 128x160 + LVGL");
    ESP_ERROR_CHECK(init_backlight());
    ESP_ERROR_CHECK(init_spi_lcd());
    ESP_ERROR_CHECK(init_lvgl());

    lvgl_port_lock(portMAX_DELAY);
    build_home_screen();
    build_qr_screen();
    build_config_screen();
    // Boot vào màn Home
    lv_screen_load(s_scr_home);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "Display ready - màn Home");
    return ESP_OK;
}

esp_err_t display_show_config_mode(const char *ap_name)
{
    if (s_scr_config == nullptr) return ESP_ERR_INVALID_STATE;

    lvgl_port_lock(portMAX_DELAY);
    // Update AP name trên màn config (nếu được truyền)
    if (ap_name) {
        // Tìm label "NamPOS-XXXX" (con thứ 3 trong scr_config) và update
        // Cách đơn giản: tìm obj có text bắt đầu bằng "NamPOS"
        uint32_t cnt = lv_obj_get_child_count(s_scr_config);
        for (uint32_t i = 0; i < cnt; i++) {
            lv_obj_t *c = lv_obj_get_child(s_scr_config, i);
            if (lv_obj_check_type(c, &lv_label_class)) {
                const char *txt = lv_label_get_text(c);
                if (txt && strstr(txt, "NamPOS")) {
                    lv_label_set_text(c, ap_name);
                    break;
                }
            }
        }
    }
    lv_screen_load(s_scr_config);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_set_wifi_status(bool connected)
{
    if (s_home_wifi == nullptr) return ESP_ERR_INVALID_STATE;
    lvgl_port_lock(portMAX_DELAY);
    if (connected) {
        lv_obj_set_style_text_color(s_home_wifi, lv_color_hex(0x00AA00), 0);   // xanh = connected
        lv_label_set_text(s_home_wifi, LV_SYMBOL_WIFI);
    } else {
        lv_obj_set_style_text_color(s_home_wifi, lv_color_hex(0xCC0000), 0);   // đỏ = disconnect
        lv_label_set_text(s_home_wifi, LV_SYMBOL_CLOSE);                        // dấu X
    }
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_show_qr(const char *emv_payload, int amount_vnd, const char *order_id)
{
    if (s_qr_img == nullptr) return ESP_ERR_INVALID_STATE;

    lvgl_port_lock(portMAX_DELAY);
    // Update QR
    if (emv_payload) {
        lv_qrcode_set_data(s_qr_img, emv_payload);
    }
    // Update số tiền
    char buf[32];
    snprintf(buf, sizeof(buf), "%d VND", amount_vnd);
    lv_label_set_text(s_qr_amount, buf);
    // Update mã đơn hàng
    lv_label_set_text(s_qr_order, order_id ? order_id : "");
    // Chuyển màn
    lv_screen_load(s_scr_qr);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_show_home(void)
{
    if (s_scr_home == nullptr) return ESP_ERR_INVALID_STATE;
    lvgl_port_lock(portMAX_DELAY);
    lv_screen_load(s_scr_home);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t display_set_title(const char *text)
{
    if (s_home_title == nullptr) return ESP_ERR_INVALID_STATE;
    lvgl_port_lock(portMAX_DELAY);
    lv_label_set_text(s_home_title, text);
    lvgl_port_unlock();
    return ESP_OK;
}

void display_lock(void)   { lvgl_port_lock(portMAX_DELAY); }
void display_unlock(void) { lvgl_port_unlock(); }
