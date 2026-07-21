# Build & Flash Guide — CucQuy Payment Notification

ESP32-WROOM 4MB + ST7735 128x160 + PCM5102A, ESP-IDF v5.5.2.

## Yêu cầu môi trường (chỉ cài 1 lần)

| Tool | Path trên máy này |
|---|---|
| ESP-IDF | `C:\Users\Nam\esp\v5.5.2\esp-idf` |
| Python venv | `C:\Users\Nam\.espressif\python_env\idf5.5_py3.11_env` |
| IDF_TOOLS_PATH | `C:\Users\Nam\.espressif` |

> Lưu ý: scoop python 3.13 trong PATH sẽ làm hỏng `export.bat` (nó cố tìm venv
> py3.13 không tồn tại). Phải set `IDF_PYTHON_ENV_PATH` trỏ vào venv py3.11.

## Build / Flash / Monitor

Mở **cmd.exe** (KHÔNG phải Git Bash — IDF không support MSys), chạy từng lệnh:

```cmd
:: === 1. Setup môi trường (chạy 1 lần mỗi phiên cmd mới) ===
set IDF_PYTHON_ENV_PATH=C:\Users\Nam\.espressif\python_env\idf5.5_py3.11_env
set IDF_TOOLS_PATH=C:\Users\Nam\.espressif
call "C:\Users\Nam\esp\v5.5.2\esp-idf\export.bat"

:: === 2. Vào project ===
cd /d D:\Work\WORK\Outsource\CucQuy_payment_notification

:: === 3. Build ===
idf.py build

:: === 4. Flash + monitor (đổi COM6 thành port thật) ===
idf.py -p COM6 flash monitor

:: === Các lệnh hữu ích khác ===
idf.py set-target esp32        :: reset target (xóa build)
idf.py fullclean               :: xóa sạch build
idf.py menuconfig              :: đổi config (LVGL, WiFi, ...)
idf.py -p COM6 monitor         :: chỉ monitor, không flash
idf.py -p COM6 erase-flash     :: xóa flash (đặt lại NVS)
```

## Lệnh one-liner (paste 1 lần là chạy được luôn)

```cmd
set IDF_PYTHON_ENV_PATH=C:\Users\Nam\.espressif\python_env\idf5.5_py3.11_env && set IDF_TOOLS_PATH=C:\Users\Nam\.espressif && call "C:\Users\Nam\esp\v5.5.2\esp-idf\export.bat" && cd /d D:\Work\WORK\Outsource\CucQuy_payment_notification && idf.py build
```

## Flash args (khi cần flash thủ công bằng esptool)

```cmd
cd /d D:\Work\WORK\Outsource\CucQuy_payment_notification\build
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset ^
  write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m ^
  0x1000  bootloader\bootloader.bin ^
  0x8000  partition_table\partition-table.bin ^
  0xd000  ota_data_initial.bin ^
  0x10000 cucquy_payment_notification.bin
```

## Pin assignment (tham khảo)

| Chức năng | GPIO |
|---|---|
| LCD ST7735 MOSI/CLK/DC/RST/CS/BL | 4 / 15 / 21 / 18 / 22 / 23 |
| PCM5102A DOUT/BCLK/LRCK | 33 / 14 / 27 |
| Boot button / LED | 0 / 2 |

## Cấu trúc project

```
main/
├── main.cc              app_main - khởi tạo tuần tự
├── config.h             pin defs + WiFi/MQTT config (đỔI ở đây)
├── display/             LCD ST7735 + LVGL + QR code
├── audio/               I2S TX cho PCM5102A
├── network/             wifi.cc + mqtt.cc (ESP-IDF built-in)
└── app/                 notify_app.cc - MQTT msg → beep + update title
```

## Lỗi thường gặp

| Lỗi | Nguyên nhân | Khắc phục |
|---|---|---|
| `idf.py is not recognized` | Chưa `call export.bat` | Chạy lệnh setup môi trường ở trên |
| `Python venv idf5.5_py3.13_env not found` | scoop py3.13 chen ngang | Set `IDF_PYTHON_ENV_PATH` như hướng dẫn |
| `This .bat file is for Windows CMD.EXE shell only` | Đang chạy trong Git Bash | Dùng `cmd.exe` thay |
| `espidf.constraints.v5.5.txt doesn't exist` | Sai `IDF_TOOLS_PATH` | Set thành `C:\Users\Nam\.espressif` |

## Thay đổi config MQTT

WiFi cấu hình qua **captive portal** lúc chạy (AP `NamPOS-XXXX` → 192.168.4.1),
KHÔNG hardcode. MQTT sửa trong `main/config.h`:
```c
#define MQTT_BROKER_URI     "wss://mqtt.cucquy.site:443/mqtt"
#define MQTT_USERNAME       "cucquy"
#define MQTT_PASSWORD       "..."   // set pass THẬT ở local, KHÔNG commit (repo public)
// Topic (khớp backend CucQuy):
#define MQTT_TOPIC_ORDER_CREATE  "cucquy/esp_01/order/create"  // hiện QR + số tiền
#define MQTT_TOPIC_ORDER_PAID    "cucquy/esp_01/order/paid"    // loa "đã nhận"
#define MQTT_TOPIC_ORDER_CANCEL  "cucquy/esp_01/order/cancel"  // về màn chính
```
Broker auth qua RiceService (credential cucquy). Sau khi sửa → build + flash lại.
