#!/usr/bin/env python3
"""v2.5.6: 版本号单一来源校验.

FXTK_VERSION 有三处对外承诺, 必须一致:
  components/fxtk/fxtk.h   #define FXTK_VERSION "x.y.z"   (窗口标题/发布包取它)
  CHANGELOG.md             最新一节 "## vx.y.z"
  README.md / docs_en/README_en.md   shields badge "version-vx.y.z"
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read(rel):
    path = os.path.join(ROOT, rel)
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as f:
        return f.read()


def main():
    head = read("components/fxtk/fxtk.h") or ""
    m = re.search(r'#define\s+FXTK_VERSION\s+"([0-9]+\.[0-9]+\.[0-9]+)"', head)
    header_ver = m.group(1) if m else None

    changelog = read("CHANGELOG.md") or ""
    m = re.search(r"^##\s+v([0-9]+\.[0-9]+\.[0-9]+)", changelog, re.M)
    changelog_ver = m.group(1) if m else None

    readme_ver = None
    m = re.search(r"version-v([0-9]+\.[0-9]+\.[0-9]+)", read("README.md") or "")
    if m:
        readme_ver = m.group(1)

    en_ver = None
    m = re.search(r"version-v([0-9]+\.[0-9]+\.[0-9]+)", read("docs_en/README_en.md") or "")
    if m:
        en_ver = m.group(1)

    found = {"fxtk.h": header_ver, "CHANGELOG.md": changelog_ver,
             "README.md": readme_ver, "docs_en/README_en.md": en_ver}
    # 只比较实际存在的来源 (英文 README 可以没有 badge)
    vals = {k: v for k, v in found.items() if v is not None}
    if not vals:
        print("[version] FAIL: 一个版本号都没找到")
        return 1
    if len(set(vals.values())) > 1:
        print("[version] FAIL: 版本号不一致:")
        for k, v in found.items():
            print("   %-22s %s" % (k, v if v is not None else "(未找到)"))
        return 1
    if header_ver is None or changelog_ver is None:
        print("[version] FAIL: 关键来源缺失 (fxtk.h / CHANGELOG.md)")
        return 1
    print("[version] OK: %s (fxtk.h / CHANGELOG / README 一致)" % header_ver)
    return 0


if __name__ == "__main__":
    sys.exit(main())
