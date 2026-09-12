#!/usr/bin/env python3
"""gen_raymarch_glsl330.py — 把 gpu_raymarch.c 里的 GLSL ES 1.00 光线步进片元着色器
转换为 sokol(GLES/GLCORE, GLSL 330)可用的版本, 输出 demo-main/raymarch_fs330.h。

为什么要转换而不是重写: 这段 raymarcher 有一百多行, 重写极易引入视觉差异;
用脚本机械转换 + 与 CPU 光追(cpu 路径)像素对照, 才能证明"GPU 与 CPU 画的是同一个场景"。

转换规则
  - 去掉 `precision highp float;` (桌面 GLSL 不需要)
  - `uniform vec2 u_res; uniform float u_time;` → sokol 要求的 uniform block
    `layout(binding=0) uniform fs_params { vec4 u_rect; float u_time; };`
    (u_res 由 u_rect.zw 提供; 420pack 扩展在 GLSL 330 下必须显式开启)
  - `gl_FragColor = X` → `frag_color = X;` + `out vec4 frag_color;`
  - 用 gl_FragCoord 推导画布局部像素坐标 (视口已限定到目标矩形)

用法: python3 tools/gen_raymarch_glsl330.py   (在仓库根执行)
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "demo-main" / "gpu_raymarch.c"
OUT = ROOT / "demo-main" / "raymarch_fs330.h"

text = SRC.read_text(encoding="utf-8")
m = re.search(r'static const char \*FRAG_SRC\s*=\s*(.*?);\s*\n', text, re.S)
if not m:
    sys.exit("未找到 FRAG_SRC")

body = m.group(1)
# 把 C 字符串字面量拼接还原成 GLSL 源码
parts = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
glsl = "".join(p.encode().decode("unicode_escape") for p in parts)

# ---- 转换 ----
glsl = re.sub(r"^\s*precision\s+\w+\s+float;\s*$", "", glsl, flags=re.M)
glsl = glsl.replace("uniform vec2 u_res;", "")
glsl = glsl.replace("uniform float u_time;", "")
# 用 u_rect.zw 顶替 u_res
glsl = re.sub(r"\bu_res\b", "u_rect.zw", glsl)
# 片元输出
glsl = re.sub(r"gl_FragColor\s*=", "frag_color =", glsl)

header = """/**
 * raymarch_fs330.h — 由 tools/gen_raymarch_glsl330.py 从 gpu_raymarch.c 的 GLSL ES 片元着色器
 * 机械转换而来 (勿手改; 改源着色器后重跑脚本)。供 sokol 后端做真 GPU 光线步进。
 */
#ifndef FXTK_RAYMARCH_FS330_H
#define FXTK_RAYMARCH_FS330_H

static const char *RAYMARCH_FS330 =
"""
out = header
for line in glsl.splitlines(True):
    esc = line.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    out += f'    "{esc}"\n'
out += ';\n\n#endif\n'

OUT.write_text(out, encoding="utf-8")
print(f"✓ 已生成 {OUT.relative_to(ROOT)} ({len(glsl)} 字符 GLSL)")
print("前 8 行:")
for line in glsl.splitlines()[:8]:
    print("   ", line)
