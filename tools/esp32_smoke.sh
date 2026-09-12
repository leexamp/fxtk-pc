#!/bin/bash
# ============================================================
# esp32_smoke.sh — ESP32 侧接口级编译冒烟 (不需要 ESP-IDF 工具链)
#
#   ./tools/esp32_smoke.sh      或   make esp32-smoke
#
# 做法: 用【最小假 IDF 头】+ `-DESP_PLATFORM` 对框架核心做 -fsyntax-only 检查。
#   框架在 ESP32 下只用到两个 IDF 符号(esp_log_write / esp_timer_get_time),
#   所以假头只需声明这两个即可, 不必装整个 IDF。
# 目的: 把"改坏了 ESP32 侧编译"这类问题挡在提交前 —— 缺头文件、签名不符、
#   用了非 C99 特性、把 POSIX 专有实现错放进公共路径, 都会在这里暴露。
# 不覆盖: `fxtk_font.c`(SDL_ttf 文本层, ESP32 用另一套文本层);
#   也不做链接/运行 —— 那是真机与 IDF 工具链的事。
# ============================================================
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/idf"

cat > "$TMP/idf/esp_log.h" <<'HDR'
#pragma once
#include <stdarg.h>
typedef enum { ESP_LOG_NONE, ESP_LOG_ERROR, ESP_LOG_WARN, ESP_LOG_INFO,
               ESP_LOG_DEBUG, ESP_LOG_VERBOSE } esp_log_level_t;
void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...);
void esp_log_level_set(const char *tag, esp_log_level_t level);
#define ESP_LOGE(tag, fmt, ...) esp_log_write(ESP_LOG_ERROR,   tag, fmt, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) esp_log_write(ESP_LOG_WARN,    tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) esp_log_write(ESP_LOG_INFO,    tag, fmt, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) esp_log_write(ESP_LOG_DEBUG,   tag, fmt, ##__VA_ARGS__)
#define ESP_LOGV(tag, fmt, ...) esp_log_write(ESP_LOG_VERBOSE, tag, fmt, ##__VA_ARGS__)
HDR

cat > "$TMP/idf/esp_timer.h" <<'HDR'
#pragma once
#include <stdint.h>
int64_t esp_timer_get_time(void);
HDR


mkdir -p "$TMP/idf/freertos"
cat > "$TMP/idf/freertos/FreeRTOS.h" <<'HDR'
#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
HDR

cat > "$TMP/idf/freertos/task.h" <<'HDR'
#pragma once
#include "freertos/FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
HDR

SRCS="fxtk.c fxtk_draw.c fxtk_widgets.c fxtk_effects.c fxtk_extra.c fxtk_backends.c"
FAIL=0
echo "🔎 ESP32 接口级冒烟 (-DESP_PLATFORM, 假 IDF 头, 只做语法/接口检查)"
for f in $SRCS; do
    printf '   %-18s ' "$f"
    # 关键: 把"隐式函数声明"等升级为错误 —— C99 下拼错的 IDF 调用默认只是警告,
    # 那样冒烟会"假通过"(负向验证实测过: 故意调用不存在的 esp_* 函数居然编过了)。
    if gcc -std=gnu99 -Wall -fsyntax-only -DESP_PLATFORM \
           -Werror=implicit-function-declaration -Werror=incompatible-pointer-types \
           -Werror=return-type -Werror=int-conversion \
           -I"$TMP/idf" -I"$ROOT/components/fxtk" "$ROOT/components/fxtk/$f" 2>"$TMP/err"; then
        echo "ok"
    else
        echo "FAIL"; sed 's/^/      /' "$TMP/err" | head -12; FAIL=1
    fi
done
[ "$FAIL" = "0" ] && echo "✅ ESP32 冒烟通过(6 个核心文件)" || echo "❌ ESP32 冒烟失败"
exit $FAIL
