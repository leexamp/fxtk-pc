#!/usr/bin/env python3
"""v2.5.6: 公开头文件声明 <-> 实现符号 一致性检查 ("声明了, 但没人实现")

为什么需要它:
  fxtk_font_height() 在 fxtk.h 里声明了很久, 两个字体后端都没实现 —— 编译能过,
  一调用就链接失败. tools/check_api_docs.py 只检查"声明有没有写进文档", 查不出这种洞.

做法 (用链接器的视角):
  1. 编译核心源码, 用 nm 取【已定义】的全局符号;
  2. 从公开头文件里抽取所有 fx_*/fxtk_* 函数声明;
  3. 声明 - 已定义 - 允许清单 = 缺失符号, 非空即失败.

两种字体后端各查一遍 (同一套头文件, 不同实现文件):
  core       : components/fxtk/fxtk*.c
  sokol-font : 去掉 fxtk_font.c, 换成 drivers/fxtk_font_stb.c

由驱动/可选模块提供的宿主 API 列在 tools/api_allowlist.txt, 每条必须写明原因.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADERS = [
    "components/fxtk/fxtk.h",
    "components/fxtk/fxtk_desktop.h",
    "components/fxtk/fxtk_effects.h",
    "components/fxtk/fxtk_image.h",
    "components/fxtk/fxtk_backends.h",
]
ALLOWLIST = "tools/api_allowlist.txt"
CC = os.environ.get("CC", "gcc")


def header_declarations():
    """返回 [(name, 'header:line'), ...] —— 与 check_api_docs.py 相同的抽取规则."""
    out = []
    for rel in HEADERS:
        path = os.path.join(ROOT, rel)
        try:
            with open(path, encoding="utf-8") as f:
                src = f.read().splitlines()
        except FileNotFoundError:
            continue
        i = 0
        while i < len(src):
            st = src[i].strip()
            if (
                "(" in st
                and re.match(r"^[A-Za-z_][A-Za-z0-9_ \*]*\b[A-Za-z_][A-Za-z0-9_]*\s*\(", st)
                and not st.startswith(("typedef", "#", "}"))
                and not st.startswith("static inline")
            ):
                decl, j = st, i
                while ";" not in decl and j + 1 < len(src):
                    j += 1
                    decl += " " + src[j].strip()
                nc = re.sub(r"/\*.*?\*/", "", decl).strip()
                m = re.match(r"^[A-Za-z_][A-Za-z0-9_ \*]*\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", nc)
                if m and nc.endswith(";") and "{" not in nc:
                    name = m.group(1)
                    if name.startswith(("fx_", "fxtk_")):
                        out.append((name, rel + ":" + str(i + 1)))
                i = j + 1
                continue
            i += 1
    return out


def allowlist():
    path = os.path.join(ROOT, ALLOWLIST)
    allowed = {}
    if not os.path.exists(path):
        return allowed
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            name = line.split()[0]
            reason = line.split("#", 1)[1].strip() if "#" in line else "(未写原因)"
            allowed[name] = reason
    return allowed


def defined_symbols(sources, extra_inc=()):
    tmp = tempfile.mkdtemp(prefix="fxtk-symcheck-")
    objs = []
    inc = ["-I" + os.path.join(ROOT, "components/fxtk")]
    inc += ["-I" + os.path.join(ROOT, d) for d in extra_inc]
    try:
        for src in sources:
            obj = os.path.join(tmp, os.path.basename(src) + ".o")
            cmd = [CC, "-O0", "-w", "-c", src, "-o", obj] + inc
            p = subprocess.run(cmd, capture_output=True, text=True)
            if p.returncode != 0:
                sys.stderr.write("[symbols] 编译失败: %s\n%s\n" % (src, p.stderr[-1500:]))
                raise SystemExit(2)
            objs.append(obj)
        nm = subprocess.run(["nm", "-g", "--defined-only"] + objs, capture_output=True, text=True)
        if nm.returncode != 0:
            sys.stderr.write("[symbols] nm 失败: " + nm.stderr + "\n")
            raise SystemExit(2)
        names = set()
        for line in nm.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[-1] not in ("U", "w"):
                names.add(parts[-1])
        return names
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def check_build(label, sources, extra_inc=()):
    decls = header_declarations()
    defined = defined_symbols(sources, extra_inc)
    allowed = allowlist()
    missing = [(n, loc) for n, loc in decls if n not in defined and n not in allowed]
    if missing:
        print("[symbols] %s: %d 个声明没有实现:" % (label, len(missing)))
        for n, loc in sorted(missing):
            print("   %-32s %s" % (n, loc))
        return False
    print("[symbols] %-10s OK: %d 个公开函数声明, 全部有实现 (允许清单 %d 条)"
          % (label, len(set(n for n, _ in decls)), len(allowed)))
    return True


def main():
    fxtk_dir = os.path.join(ROOT, "components/fxtk")
    core_all = sorted(
        os.path.join(fxtk_dir, f) for f in os.listdir(fxtk_dir)
        if f.startswith("fxtk") and f.endswith(".c")
    )
    sdl_core = list(core_all)                      # 含 fxtk_font.c (SDL_ttf)
    stb_core = [p for p in core_all if not p.endswith("fxtk_font.c")]
    stb_core.append(os.path.join(ROOT, "drivers/fxtk_font_stb.c"))

    ok = True
    ok = check_build("sokol-font", stb_core, extra_inc=("third_party/sokol",)) and ok
    ok = check_build("sdl-font", sdl_core, extra_inc=("third_party/sokol",)) and ok
    if not ok:
        print("\n[symbols] FAIL —— 公开头文件里的声明必须在所有字体后端里都有实现;")
        print("          确实由驱动/可选模块提供的, 写进 " + ALLOWLIST + " 并注明原因.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
