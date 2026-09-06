#ifndef ESP_LOG_H
#define ESP_LOG_H

/*
 * esp_log.h — PC 端日志宏 (仅 fxtk.c 核心库在桌面构建时使用)
 * 注意: 这里只是 log 输出宏的等价实现, 不模拟任何 ESP32/FreeRTOS API。
 *       ESP32 真机构建时使用 ESP-IDF 自带的 esp_log.h。
 */

#include <stdio.h>
#include <stdint.h>

#define ESP_LOGI(tag, fmt, ...) printf("\033[32m[I] %s:\033[0m " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("\033[31m[E] %s:\033[0m " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("\033[33m[W] %s:\033[0m " fmt "\n", tag, ##__VA_ARGS__)

#endif
