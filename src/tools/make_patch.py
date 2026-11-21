# パッチデータ（build/patch.bin）を作る: 字形（美咲 8x8）、禁則、訳（原文の FNV-1a → 単位列）、パラグラフ画像
#   text/ja.json : {原文: 訳}（原文は抽出したままの文字列。制御の字は \r \x0b など）
#   text/fix.json: 後から直す分（同じ形、こちらが優先）
import sys, os, json, struct, re, unicodedata
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))

def load_bdf(path):
    """Unicode → 8 行のビット（左が MSB）。美咲は FONTBOUNDINGBOX 8 8 0 -1（下 1 行が descent）"""
    g = {}
    cur = None; bm = []; inb = False; bbx = None
    for line in open(path, encoding='latin-1'):
        if line.startswith('ENCODING'):
            cur = int(line.split()[1])
        elif line.startswith('BBX'):
            bbx = [int(v) for v in line.split()[1:]]
        elif line.startswith('BITMAP'):
            inb = True; bm = []
        elif line.startswith('ENDCHAR'):
            inb = False
            w, h, xo, yo = bbx
            rows = [0] * 8
            top = 7 - h - yo
            for i, v in enumerate(bm):
                y = top + i
                if 0 <= y < 8:
                    rows[y] = (v >> xo) & 0xff
            g[cur] = rows
        elif inb:
            bm.append(int(line.strip()[:2], 16))
    return g

NO_START = set('、。，．・：；？！ー」』）】〕〉》ゝゞヽヾぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ…‥!?.,)')
NO_END = set('「『（【〔〈《(')

def build(trans, out):
    bdf = load_bdf(os.path.join(ROOT, 'font', 'misaki_gothic_2nd.bdf'))
    glyph_of = {}      # 字 → 番号
    glyphs = []
    kin = []
    def gid(ch):
        if ch in glyph_of: return glyph_of[ch]
        code = ord(ch)
        if code not in bdf:
            raise SystemExit('no glyph for %r (U+%04X)' % (ch, ord(ch)))
        glyph_of[ch] = len(glyphs)
        glyphs.append(bytes(bdf[code]))
        kin.append((1 if ch in NO_START else 0) | (2 if ch in NO_END else 0))
        return glyph_of[ch]
    entries = {}
    for src, ja in trans.items():
        units = []
        for ch in ja:
            o = ord(ch)
            if o < 0x80 or 0x80 <= o < 0xa0:
                units.append(o)
                continue
            if ch == '　':
                units.append(0x20); continue
            n = unicodedata.normalize('NFKC', ch)
            if len(n) == 1 and 0x21 <= ord(n) < 0x7f and ch not in '，．：；？！（）［］｛｝＜＞＝＋－＊／＠＃＄％＆＿｜～':
                units.append(ord(n)); continue
            units.append(0x100 + gid(ch))
        key = src.encode('latin-1')
        h = fnv(key)
        if h in entries and entries[h][0] != key:
            raise SystemExit('hash collision %r %r' % (src, entries[h][0]))
        entries[h] = (key, units)
    secs = []
    g = struct.pack('<I', len(glyphs)) + b''.join(glyphs)
    secs.append((b'G', g))
    secs.append((b'K', bytes(kin)))
    idx = b''; data = b''
    for h in sorted(entries):
        units = entries[h][1]
        idx += struct.pack('<II', h, len(data))
        data += struct.pack('<H', len(units)) + struct.pack('<%dH' % len(units), *units)
    secs.append((b'T', struct.pack('<I', len(entries)) + idx + data))
    para = os.path.join(ROOT, 'build', 'para.lz')
    if os.path.exists(para):
        secs.append((b'P', open(para, 'rb').read()))
    blob = b'WLJ1' + struct.pack('<I', len(secs))
    for t, d in secs:
        blob += t + struct.pack('<I', len(d)) + d
    open(out, 'wb').write(blob)
    print('glyphs', len(glyphs), 'entries', len(entries), 'bytes', len(blob))

def fnv(b):
    h = 0x811c9dc5
    for c in b:
        h ^= c; h = (h * 0x01000193) & 0xffffffff
    return h

if __name__ == '__main__':
    trans = {}
    names = ['ja.json', 'fix.json'] + (['fix_test.json'] if os.environ.get('WLJP_TEST') else [])   # fix_test: テスト専用（配布しない）
    for fn in names:
        p = os.path.join(ROOT, 'text', fn)
        if os.path.exists(p):
            trans.update(json.load(open(p, encoding='utf-8')))
    build(trans, os.path.join(ROOT, 'build', 'patch.bin'))
