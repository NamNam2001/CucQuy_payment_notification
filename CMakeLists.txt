# CMakeLists.txt - CucQuy Payment Notification
# ESP32-WROOM (4MB flash) + ST7735 128x160 + PCM5102A
cmake_minimum_required(VERSION 3.16)

# Tắt warning "missing-field-initializers" rất phổ biến với struct ESP-IDF
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(cucquy_payment_notification)
