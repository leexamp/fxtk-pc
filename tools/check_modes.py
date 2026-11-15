#!/usr/bin/env python3
"""v2.5.6: git 索引里所有 *.sh 必须具备可执行位 (100755).

背景: CI 曾三个 job 全红, 根因是 18 个 *.sh 在 git 索引里是 100644 —— 本地文件
系统恰好是 755, 复现不出来; 只有 'git ls-files -s' 看得出差别. 现在把它变成闸门.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    try:
        out = subprocess.run(["git", "-C", ROOT, "ls-files", "-s"],
                             capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        print("[modes] 跳过: 无法执行 git (%s)" % e)
        return 0
    bad = []
    for line in out.splitlines():
        parts = line.split("\t")
        if len(parts) != 2:
            continue
        meta, path = parts
        cols = meta.split()
        if len(cols) != 3:
            continue
        mode, path = cols[0], path
        if path.endswith(".sh") and mode != "100755":
            bad.append((mode, path))
    if bad:
        print("[modes] FAIL: %d 个 *.sh 在 git 索引里没有可执行位 (100755):" % len(bad))
        for mode, path in bad:
            print("   %s  %s" % (mode, path))
        print("修复: chmod +x <file> && git update-index --chmod=+x <file>")
        return 1
    print("[modes] OK: 所有 *.sh 在 git 索引里都是 100755")
    return 0


if __name__ == "__main__":
    sys.exit(main())
