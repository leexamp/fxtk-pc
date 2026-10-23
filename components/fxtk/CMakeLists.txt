# ⚠️ 已停用 (v2.5.3): ESP32 路径不再维护, 本文件仅作历史存档; CI / make 都不再用它。
#    注意 SRCS 清单已过期 (缺 fxtk_gesture/fxtk_flex/fxtk_svg/fxtk_par), 直接拿去构建 ESP-IDF
#    工程会缺符号 —— 保留它只是为了不让"曾经支持过"这段历史断掉, 不代表可用。
#
# fxtk 库组件: 被 demo (根工程 main) 和 examples/* 共同引用
# v2.3.1: 补齐 fxtk_effects/fxtk_extra/fxtk_fs — 旧清单缺源, 组件链接必败 (undefined fx_draw_image_rot 等)
idf_component_register(
    SRCS "fxtk.c" "fxtk_draw.c" "fxtk_font.c" "fxtk_widgets.c"
         "fxtk_effects.c" "fxtk_extra.c" "fxtk_backends.c"
    INCLUDE_DIRS "."
    REQUIRES driver)
