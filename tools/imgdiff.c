/**
 * imgdiff.c — v2.4 金图回归的像素比对工具 (零外部依赖: 用仓库内 vendored stb)
 *
 *   imgdiff <a.png> <b.png> [diff_out.png] [容差]
 *
 * 逐像素比较, 打印差异像素数/最大通道差, 并把差异可视化到 diff_out(红=不同)。
 * 退出码: 0 = 一致(在容差内), 1 = 有差异, 2 = 出错。
 * 容差默认 0(逐字节一致); 跨平台/跨驱动比对时可给 2~4 容忍编码与采样相位差异。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STB_IMAGE_IMPLEMENTATION
#include "../components/fxtk/vendor/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../components/fxtk/vendor/stb/stb_image_write.h"

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "用法: %s a.png b.png [diff_out.png] [容差]\n", argv[0]); return 2; }
    int tol = argc > 4 ? atoi(argv[4]) : 0;
    int wa, ha, na, wb, hb, nb;
    unsigned char *A = stbi_load(argv[1], &wa, &ha, &na, 4);
    unsigned char *B = stbi_load(argv[2], &wb, &hb, &nb, 4);
    if (!A) { fprintf(stderr, "打不开: %s\n", argv[1]); return 2; }
    if (!B) { fprintf(stderr, "打不开: %s\n", argv[2]); return 2; }
    if (wa != wb || ha != hb) {
        fprintf(stderr, "尺寸不同: %dx%d vs %dx%d\n", wa, ha, wb, hb);
        return 1;
    }
    long n = (long)wa * ha;
    long diff = 0; int worst = 0;
    unsigned char *D = NULL;
    if (argc > 3) { D = (unsigned char *)malloc((size_t)n * 4); if (D) memcpy(D, B, (size_t)n * 4); }
    for (long i = 0; i < n; i++) {
        int d = 0;
        for (int c = 0; c < 4; c++) {
            int v = abs((int)A[i*4+c] - (int)B[i*4+c]);
            if (v > d) d = v;
        }
        if (d > worst) worst = d;
        if (d > tol) {
            diff++;
            if (D) { D[i*4+0] = 255; D[i*4+1] = 0; D[i*4+2] = 0; D[i*4+3] = 255; }
        }
    }
    if (D) { stbi_write_png(argv[3], wa, ha, 4, D, wa * 4); free(D); }
    if (diff == 0) {
        printf("  一致 %s (%ld 像素, 最大通道差 %d, 容差 %d)\n", argv[1], n, worst, tol);
    } else {
        printf("  差异 %s vs %s: %ld/%ld 像素不同 (%.2f%%), 最大通道差 %d\n",
               argv[1], argv[2], diff, n, 100.0 * diff / n, worst);
    }
    free(A); free(B);
    return diff == 0 ? 0 : 1;
}
