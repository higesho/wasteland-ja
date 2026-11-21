# 訳の検査: 記号の数と順、先頭・末尾の改行、メニューの頭文字、paragraph 番号、字形の有無
#   python src/tools/check.py            … work/out の全部と text/ui.json を調べる
import sys, os, json, re, glob, unicodedata
sys.path.insert(0, os.path.dirname(__file__))
from tok import TOKRE
from make_patch import load_bdf
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
BDF = load_bdf(os.path.join(ROOT, 'font', 'misaki_gothic_2nd.bdf'))
SWITCH = ('{PL}', '{SEX}', '{PRON}')

def strip_switch(toks):
    return [t for t in toks if t not in SWITCH]

def lead(s):
    return len(s) - len(s.lstrip('\n'))
def trail(s):
    return len(s) - len(s.rstrip('\n'))

def check(en, ja):
    errs = []
    te = TOKRE.findall(en); tj = TOKRE.findall(ja)
    se = strip_switch(te); sj = strip_switch(tj)
    # NAME と NUM は入れ替えてよい
    def norm(l): return ['{V}' if t in ('{NAME}', '{NUM}') else t for t in l]
    if norm(se) != norm(sj) or sorted(se) != sorted(sj):
        errs.append('tokens %s -> %s' % (''.join(se), ''.join(sj)))
    for s in SWITCH:
        c = tj.count(s)
        if c and c != te.count(s) and not (s == '{PL}' and c % 3 == 0) and not (s == '{SEX}' and c % 3 == 0) and not (s == '{PRON}' and c % 4 == 0):
            errs.append('switch %s %d->%d' % (s, te.count(s), c))
    if lead(en) != lead(ja): errs.append('lead nl %d->%d' % (lead(en), lead(ja)))
    if trail(en) != trail(ja): errs.append('trail nl %d->%d' % (trail(en), trail(ja)))
    # メニュー項目
    ie = re.findall(r'\{ITEM\}(.)', en); ij = re.findall(r'\{ITEM\}(.)(.?)', ja)
    for a, (b, c) in zip(ie, ij):
        if a != b: errs.append('item key %r -> %r' % (a, b))
    for m in re.findall(r'\{ITEM\}([^\n{]*)', ja):
        if not re.match(r'^(.\)|\d\) )', m) and re.search(r'[^\x00-\x7f]', m):
            errs.append('item form %r' % m)
    # パラグラフ番号
    pe = re.findall(r'paragraph\s+(\d+)', en, re.I); pj = re.findall(r'paragraph\s+(\d+)', ja, re.I)
    if sorted(pe) != sorted(pj): errs.append('paragraph %s -> %s' % (pe, pj))
    # 字形
    for ch in ja:
        o = ord(ch)
        if o < 0x80: continue
        if ch == '　': continue
        n = unicodedata.normalize('NFKC', ch)
        if len(n) == 1 and 0x21 <= ord(n) < 0x7f and ch not in '，．：；？！（）［］｛｝＜＞＝＋－＊／＠＃＄％＆＿｜～':
            continue
        if o not in BDF: errs.append('no glyph %r' % ch)
    if re.search(r'[Ａ-Ｚａ-ｚ０-９]', ja): errs.append('fullwidth alnum')
    if '"' in ja and '"' not in en: errs.append('ascii quote')
    return errs

def main():
    bad = 0; total = 0
    for f in sorted(glob.glob(os.path.join(ROOT, 'work', 'chunks', '[0-9]*.json'))):
        n = os.path.basename(f)
        c = json.load(open(f, encoding='utf-8'))
        r = json.load(open(os.path.join(ROOT, 'work', 'out', n), encoding='utf-8'))
        for it in c['items']:
            total += 1
            ja = r.get(str(it['k']))
            if ja is None: print(n, it['k'], 'MISSING'); bad += 1; continue
            e = check(it['en'], ja)
            if e:
                bad += 1
                print('%s %d: %s\n   EN %r\n   JA %r' % (n, it['k'], '; '.join(e), it['en'][:160], ja[:160]))
    ids = {}
    for l in open(os.path.join(ROOT, 'work', 'ui_en.txt'), encoding='utf-8'):
        i, t = l.rstrip('\n').split('\t', 1); ids[i] = json.loads(t)
    for k, ja in json.load(open(os.path.join(ROOT, 'text', 'ui.json'), encoding='utf-8')).items():
        total += 1
        e = check(ids[k], ja)
        if e: bad += 1; print('ui %s: %s\n   EN %r\n   JA %r' % (k, '; '.join(e), ids[k][:160], ja[:160]))
    print('checked', total, 'bad', bad)

if __name__ == '__main__':
    main()
