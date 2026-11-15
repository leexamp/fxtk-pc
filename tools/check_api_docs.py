#!/usr/bin/env python3
"""公开头文件 ↔ 文档一致性检查 (v2.5.6).

三重校验, 任何一项不通过都返回 1:
  1. 覆盖: 头文件里每个 fx_*/fxtk_* 声明都必须在文档中出现 (旧版已有的检查);
  2. 参数个数: docs/api.md 里以调用形式写出的签名, 参数个数必须与头文件原型一致
     (文档写错一个参数光靠肉眼很难发现, 这条能直接抓住);
  3. 反向: 文档里以"函数("形式提到的名字必须真实声明在头文件里 ——
     防止删掉/改名的 API 在文档里留下幽灵条目 (例如曾经不存在的 fxtk_win_shutdown)。

允许清单只放"文档确实该提、但不在这些公开头文件里"的符号: 内部 helper 与二进制名。
"""
import io, re, sys, os, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADERS = [
    'components/fxtk/fxtk.h', 'components/fxtk/fxtk_wm.h', 'components/fxtk/fxtk_backends.h',
    'components/fxtk/fxtk_desktop.h', 'components/fxtk/fxtk_effects.h', 'components/fxtk/fxtk_image.h',
    'drivers/fxtk_win.h', 'components/fxtk/fxtk_fs.h',
]
# 覆盖/反向扫描用的文档 (Lua 文档里的 fx_image_* 是绑定层名字, 不在此列)
DOCS = ['docs/api.md', 'docs/multi-window.md', 'docs/window-manager.md', 'docs/backends.md',
        'docs/desktop.md', 'docs/internals.md']
# 文档可提及但不在上述公开头文件里的符号 (内部 helper / 二进制名 / Lua 绑定名)
ALLOW = {
    'fxtk_alloc', 'fxtk_free', 'fxtk_scroll_step', 'fxtk_sokol_frame',   # internals.md 描述内部流程
    'fxtk_mw_register',                                                   # drivers/fxtk_mw.h 的平台外壳注册
    'fxtk_sim', 'fxtk_lua_runner', 'fxtk_lua',                            # 二进制名
    'fx_image_size', 'fx_image_get_px', 'fx_image_fill', 'fx_image_line', # Lua 绑定层名字
}


def strip_comments(s):
    return re.sub(r'/\*.*?\*/', '', s, flags=re.S)


def split_args(inner):
    inner = inner.strip()
    if inner == '' or re.fullmatch(r'(void|\.\.\.)', inner):
        return 0
    depth, n = 0, 1
    for ch in inner:
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        elif ch == ',' and depth == 0:
            n += 1
    return n


def protos(path):
    """头文件里的公开符号 → {名字: 参数个数或 None}.
    收录: 函数原型(; 结尾)、static inline 定义、#define 工厂宏 (宏的参数个数不可靠 → None)。"""
    p = os.path.join(ROOT, path)
    if not os.path.exists(p):
        return {}
    src = strip_comments(io.open(p, encoding='utf-8').read())
    out = {}
    for m in re.finditer(r'([A-Za-z_][A-Za-z0-9_ \t\*]*?)\b((?:fx_|fxtk_)[A-Za-z0-9_]+)\s*\(', src):
        name = m.group(2)
        i, depth, j = m.end(), 1, m.end()
        while j < len(src) and depth > 0:
            if src[j] == '(':
                depth += 1
            elif src[j] == ')':
                depth -= 1
            j += 1
        if depth != 0:
            continue
        k = j
        while k < len(src) and src[k] in ' \t\r\n':
            k += 1
        is_decl = k < len(src) and src[k] == ';'
        is_inline = k < len(src) and src[k] == '{' and 'static inline' in src[max(0, m.start() - 8):j]
        if is_decl or is_inline:
            out.setdefault(name, split_args(src[i:j - 1]))
    for m in re.finditer(r'#define\s+((?:fx_|fxtk_)[A-Za-z0-9_]+)\s*\(', src):
        out.setdefault(m.group(1), None)   # 宏: 只校验存在, 不校验参数个数
    return out


def doc_calls(text):
    """文档里的调用形式 → {名字: {参数个数...}}; 跳过带 ... 的省略写法."""
    out = {}
    for m in re.finditer(r'\b((?:fx_|fxtk_)[A-Za-z0-9_]+)\s*\(', text):
        name = m.group(1)
        i, depth, j = m.end(), 1, m.end()
        while j < len(text) and depth > 0:
            if text[j] == '(':
                depth += 1
            elif text[j] == ')':
                depth -= 1
            j += 1
        if depth != 0:
            continue
        inner = text[i:j - 1]
        if '...' in inner:
            continue
        out.setdefault(name, set()).add(split_args(inner))
    return out


declared = {}
for h in HEADERS:
    for n, a in protos(h).items():
        declared.setdefault(n, a)

missing = []
for n in declared:
    if all(n not in io.open(os.path.join(ROOT, d), encoding='utf-8').read() for d in DOCS):
        missing.append(n)

doc_text = {d: io.open(os.path.join(ROOT, d), encoding='utf-8').read() for d in DOCS}
api_calls = doc_calls(doc_text['docs/api.md'])

bad_arity = []
for n, a in declared.items():
    if a is not None and n in api_calls and a not in api_calls[n]:
        bad_arity.append((n, a, sorted(api_calls[n])))

ghosts = []
for d, txt in doc_text.items():
    for n in doc_calls(txt):
        if n not in declared and n not in ALLOW:
            ghosts.append((d, n))

# 顺带扫描 README 的调用写法 (只查幽灵, 不查覆盖)
readme = io.open(os.path.join(ROOT, 'README.md'), encoding='utf-8').read()
for n in doc_calls(readme):
    if n not in declared and n not in ALLOW and n not in ('fx_xxx_new',):
        ghosts.append(('README.md', n))

print('headers:', len(HEADERS), ' declared:', len(declared))
fail = False
if missing:
    fail = True
    print('MISSING in docs (%d):' % len(missing))
    for n in sorted(set(missing)):
        print('  ', n)
if bad_arity:
    fail = True
    print('ARITY mismatch (%d):' % len(bad_arity))
    for n, a, got in bad_arity:
        print('   %s: header=%d args, docs=%s' % (n, a, got))
if ghosts:
    fail = True
    print('GHOST symbols in docs (%d):' % len(set(n for _, n in ghosts)))
    for d, n in sorted(set(ghosts)):
        print('   %s  | %s' % (n, d))
if fail:
    sys.exit(1)
print('API docs OK: coverage + arity + no ghosts')
