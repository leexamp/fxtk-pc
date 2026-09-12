# fxtk 库组件: 被 demo (根工程 main) 和 examples/* 共同引用
# v2.3.1: 补齐 fxtk_effects/fxtk_extra/fxtk_fs — 旧清单缺源, 组件链接必败 (undefined fx_draw_image_rot 等)
idf_component_register(
    SRCS "fxtk.c" "fxtk_draw.c" "fxtk_font.c" "fxtk_widgets.c"
         "fxtk_effects.c" "fxtk_extra.c" "fxtk_backends.c"
    INCLUDE_DIRS "."
    REQUIRES driver)
