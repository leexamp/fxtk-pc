/* ppm2png.c — PPM(P6) → PNG 转换 (v2.4 工具)
 *
 * 用途: fxtk 的无头渲染/截图默认输出 PPM(零依赖), 这个工具把它转成 PNG 以便查看、
 *       进文档/CI 金图对比、以及交给图像评审流程。
 * 依赖: third_party/stb/stb_image_write.h (已 vendored)
 *
 * 编译: gcc -O2 -Ithird_party/stb tools/ppm2png.c -o /tmp/ppm2png -lm
 * 用法: ppm2png in.ppm out.png    |    ppm2png *.ppm   (批量, 同名换扩展)
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[3] = {0};
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0) { fclose(f); return NULL; }
    /* 跳过注释与空白, 读宽高与最大值 */
    int vals[3] = {0, 0, 0}, got = 0, c;
    while (got < 3) {
        c = fgetc(f);
        if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n') {} continue; }
        if (c == EOF) { fclose(f); return NULL; }
        if (c >= '0' && c <= '9') {
            int v = 0;
            do { v = v * 10 + (c - '0'); c = fgetc(f); } while (c >= '0' && c <= '9');
            vals[got++] = v;
            if (c == EOF) break;
            if (c != '#') ungetc(c, f); else { while ((c = fgetc(f)) != EOF && c != '\n') {} }
        }
    }
    if (got < 3 || vals[0] <= 0 || vals[1] <= 0) { fclose(f); return NULL; }
    size_t n = (size_t)vals[0] * vals[1] * 3;
    unsigned char *px = (unsigned char *)malloc(n);
    if (!px) { fclose(f); return NULL; }
    if (fread(px, 1, n, f) != n) { free(px); fclose(f); return NULL; }
    fclose(f);
    *w = vals[0]; *h = vals[1];
    return px;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "用法: %s in.ppm [out.png] | %s *.ppm\n", argv[0], argv[0]); return 2; }
    int last = (argc == 3) ? 2 : argc;   /* argc==3: 只处理 argv[1], argv[2] 是输出名 */
    for (int i = 1; i < last; i++) {
        const char *in = argv[i];
        char out[1024];
        if (argc == 3) { snprintf(out, sizeof(out), "%s", argv[2]); }
        else {
            snprintf(out, sizeof(out), "%s", in);
            char *dot = strrchr(out, '.');
            if (dot) snprintf(dot, sizeof(out) - (size_t)(dot - out), ".png");
            else snprintf(out + strlen(out), sizeof(out) - strlen(out), ".png");
        }
        int w = 0, h = 0;
        unsigned char *px = read_ppm(in, &w, &h);
        if (!px) { fprintf(stderr, "✗ 无法读取 %s\n", in); continue; }
        if (!stbi_write_png(out, w, h, 3, px, w * 3)) { fprintf(stderr, "✗ 写 PNG 失败 %s\n", out); free(px); continue; }
        printf("✓ %s (%dx%d) → %s\n", in, w, h, out);
        free(px);
    }
    return 0;
}
