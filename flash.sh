#!/usr/bin/env bash
# =============================================================================
# flash.sh — Build + nạp firmware CucQuy POS vào ESP32 (chạy 1 phát từ A→Z).
#
#   ./flash.sh                 # tự dò cổng USB
#   ./flash.sh /dev/cu.usbserial-XXXX   # chỉ định cổng
#   ./flash.sh build           # chỉ build, không flash
#
# MQTT password KHÔNG hardcode (repo public): đọc theo thứ tự
#   1) biến môi trường MQTT_PASSWORD
#   2) file .mqtt_pass (gitignored) cạnh script
#   3) hỏi trực tiếp
# Pass được chèn tạm vào config.h lúc build rồi TỰ khôi phục placeholder khi xong.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

IDF="${IDF_PATH:-$HOME/esp/esp-idf}"
CFG="main/config.h"
PLACEHOLDER="REPLACE_WITH_MQTT_PASSWORD"

# 1) ESP-IDF env
[ -f "$IDF/export.sh" ] || { echo "❌ Chưa cài ESP-IDF ở $IDF (xem BUILD.md)"; exit 1; }
echo "▶ Nạp ESP-IDF ($IDF)…"
. "$IDF/export.sh" >/dev/null

# 2) Chế độ build-only?
MODE="flash"; PORT=""
case "${1:-}" in
  build) MODE="build" ;;
  /dev/*) PORT="$1" ;;
  "") ;;
  *) echo "Tham số lạ: $1"; exit 1 ;;
esac

# 3) MQTT password (env > .mqtt_pass > hỏi)
PASS="${MQTT_PASSWORD:-}"
[ -z "$PASS" ] && [ -f .mqtt_pass ] && PASS="$(tr -d '\r\n' < .mqtt_pass)"
if [ -z "$PASS" ]; then read -rsp "Nhập MQTT password: " PASS; echo; fi
[ -z "$PASS" ] && { echo "❌ Thiếu MQTT password"; exit 1; }

# 4) Chèn pass thật vào config.h + luôn khôi phục placeholder khi thoát
restore() { git checkout -- "$CFG" 2>/dev/null || sed -i '' "s|\"${PASS}\"|\"${PLACEHOLDER}\"|" "$CFG"; }
trap restore EXIT
sed -i '' "s|\"${PLACEHOLDER}\"|\"${PASS}\"|" "$CFG"

# 5) Set target lần đầu (nếu chưa có sdkconfig)
[ -f sdkconfig ] || { echo "▶ set-target esp32…"; idf.py set-target esp32; }

# 6) Build
echo "▶ Build…"
idf.py build

if [ "$MODE" = "build" ]; then
  echo "✅ Build xong (chưa flash). Chạy './flash.sh' để nạp."
  exit 0
fi

# 7) Dò cổng nếu chưa chỉ định
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1 || true)"
fi
[ -z "$PORT" ] && { echo "❌ Không thấy cổng ESP32. Cắm chip + cài driver USB, hoặc: ./flash.sh /dev/cu.xxxx"; exit 1; }

# 8) Flash + monitor (Ctrl+] để thoát monitor)
echo "▶ Flash + monitor qua $PORT  (Ctrl+] để thoát)…"
idf.py -p "$PORT" flash monitor
