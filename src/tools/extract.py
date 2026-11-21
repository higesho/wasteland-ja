# 原文の抽出: work/strings.json に [{id, src, text}] を書く
#  id: exe:<表>:<番号> / g1:<マップ>:<番号> / mon:... / npc:... / plain:<DS の位置>
import sys, os, json, struct, re
sys.path.insert(0, os.path.dirname(__file__))
from wltext import *
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
u = open(os.path.join(ROOT, 'work', 'WL_unp.exe'), 'rb').read()
hp = struct.unpack_from('<H', u, 8)[0] * 16
img = u[hp:]
DS = 0xce20
out = []

def add(id_, s, **kw):
    t = s.decode('latin-1')
    d = {'id': id_, 'text': t}
    d.update(kw)
    out.append(d)

for name, off, cnt in EXE_TABLES:
    for n, s in decode_group_strings(img, off - 0x200, cnt):
        if s: add('exe:%s:%d' % (name, n), s)

for disk, fn in ((1, 'GAME1'), (2, 'GAME2')):
    d = open(os.path.join(ROOT, 'orig', 'rom', 'data', fn), 'rb').read()
    for i, (a, b) in enumerate(msq_blocks(d)):
        dec = decrypt(d[a:b])
        ms, cd = map_info(dec)
        if not ms:
            continue
        so, mno, mdo = struct.unpack_from('<HHH', dec, cd)
        npco = struct.unpack_from('<H', dec, cd + 0x28)[0]
        body = bytearray(dec[:so]) + d[a + 6 + so:b]
        for n, s in decode_group_strings(body, so):
            if s: add('g%d:%d:%d' % (disk, i, n), s)
        # 怪物の名前（ASCII、0 区切り）。数 = (strings - monsterData) / 8
        if mdo:
            nm = (so - mdo) // 8
            p = mno
            for k in range(nm):
                e = body.index(b'\0', p)
                add('mon%d:%d:%d' % (disk, i, k), bytes(body[p:e]))
                p = e + 1
        # NPC（0x100 バイトの記録。名前 +0、階級 +0x32）
        if npco:
            first = struct.unpack_from('<H', body, npco)[0]
            k = 0
            while True:
                po = struct.unpack_from('<H', body, npco + 2 * k)[0] if npco + 2 * k + 2 <= len(body) else 0
                if k and (npco + 2 * k >= first or po == 0): break
                if po + 0x100 > len(body): break
                rec = body[po:po + 0x100]
                add('npc%d:%d:%d' % (disk, i, k), rec[:rec.index(b'\0')] if b'\0' in rec[:14] else rec[:13])
                rk = rec[0x32:0x32 + 25]
                add('rank%d:%d:%d' % (disk, i, k), rk[:rk.index(b'\0')] if b'\0' in rk else rk)
                k += 1
                if k > 40: break

# EXE の平文（DS の 0 終わりの文字列。英字を含む 3 文字以上）
seg = img[DS:DS + 0x10000]
for m in re.finditer(rb'[\x01-\x7f]{2,}\x00', seg):
    s = m.group()[:-1]
    if re.search(rb'[A-Za-z]{2}', s) and sum(32 <= c < 127 or c in (13, 6, 7, 3, 8, 11, 15) for c in s) == len(s):
        add('plain:%04x' % m.start(), s, ds=m.start())

json.dump(out, open(os.path.join(ROOT, 'work', 'strings.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print(len(out))
