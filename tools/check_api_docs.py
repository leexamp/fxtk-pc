
import io, re, sys, os
ROOT='/media/user/Data/fxtk-pc-main'
HEADERS=[
 'components/fxtk/fxtk.h','components/fxtk/fxtk_wm.h','components/fxtk/fxtk_backends.h',
 'components/fxtk/fxtk_desktop.h','components/fxtk/fxtk_effects.h','components/fxtk/fxtk_image.h',
 'drivers/fxtk_win.h','components/fxtk/fxtk_fs.h',
]
def names(path):
    try: src=io.open(ROOT+'/'+path,encoding='utf-8').read().splitlines()
    except FileNotFoundError: return []
    out=[]; i=0
    while i<len(src):
        st=src[i].strip()
        if '(' in st and re.match(r'^[A-Za-z_][A-Za-z0-9_ \*]*\b[A-Za-z_][A-Za-z0-9_]*\s*\(', st) and not st.startswith(('typedef','#','}')) and not st.startswith('static inline'):
            decl=st; j=i
            while ';' not in decl and j+1<len(src):
                j+=1; decl+=' '+src[j].strip()
            nc=re.sub(r'/\*.*?\*/','',decl).strip()
            m=re.match(r'^[A-Za-z_][A-Za-z0-9_ \*]*\b([A-Za-z_][A-Za-z0-9_]*)\s*\(', nc)
            if m and nc.endswith(';') and '{' not in nc:
                n=m.group(1)
                if n.startswith(('fx_','fxtk_')):
                    out.append(n)
            i=j+1; continue
        if st.startswith('#define'):
            m=re.match(r'#define\s+(fx_[A-Za-z0-9_]+)\(', st)
            if m: out.append(m.group(1))
        i+=1
    return out
docs=''
for rel in ['docs/api.md','docs/multi-window.md','docs/window-manager.md','docs/backends.md','docs/desktop.md','docs/internals.md']:
    docs += io.open(ROOT+'/'+rel,encoding='utf-8').read()
missing=[]
for h in HEADERS:
    for n in names(h):
        if n not in docs: missing.append((h,n))
print('total headers:',len(HEADERS))
if missing:
    print('MISSING %d:'%len(missing))
    seen=set()
    for h,n in missing:
        if n in seen: continue
        seen.add(n); print(' ',h,n)
    sys.exit(1)
print('API docs coverage OK')
