# パラグラフ画像（paragraphs.bin）を日本語で作り直して build/para.lz（LZ4 ブロック、先頭に u32 元の大きさ）にする
#   元の構造: 画像 13 枚（"paragraph" と数字の型など）+ 0 の dword 2 つ + 画像 165 枚（p000..p164）+ RGBA の残り
#   画像 = {u32 幅, u32 高さ, 8 ビットの濃さ（4 バイト境界まで詰める）}
#   訳は work/out/pNN.json（{番号: 日本語}）。help の p162/p163 と模様の p164 は元のまま
#   python src/tools/para_build.py [--png]   （--png: build/para_png/ に確認用の PNG も書く）
import sys, os, json, glob, struct, unicodedata
from PIL import Image, ImageDraw, ImageFont
import lz4.block
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = r'L:\SteamLibrary\steamapps\common\Wasteland\paragraphs.bin'
FONT = r'C:\Windows\Fonts\NotoSansJP-VF.ttf'
SIZE = 17
PITCH = 22
WIDTH = 640
LEFT = 6
TOP = 3
NO_START = set('、。，．・：；？！ー」』）】〕〉》…‥ぁぃぅぇぉっゃゅょゎァィゥェォッャュョヮヵヶ!?.,)')
NO_END = set('「『（【〔〈《(')

def parse(d):
    pos = 0
    head = []
    for i in range(13):
        w, h = struct.unpack_from('<II', d, pos)
        n4 = (w * h + 3) & ~3
        head.append(d[pos:pos + 8 + n4]); pos += 8 + n4
    zeros = d[pos:pos + 8]; pos += 8
    imgs = []
    for i in range(165):
        w, h = struct.unpack_from('<II', d, pos)
        n4 = (w * h + 3) & ~3
        imgs.append(d[pos:pos + 8 + n4]); pos += 8 + n4
    return head, zeros, imgs, d[pos:]

def load_font():
    f = ImageFont.truetype(FONT, SIZE)
    try:
        f.set_variation_by_axes([450])
    except Exception:
        pass
    return f

def tokens(text):
    """改行できる単位: 英数字の語（後ろの空白込み）か 1 字"""
    out = []
    i = 0
    while i < len(text):
        c = text[i]
        if c == '\n':
            out.append('\n'); i += 1; continue
        if ord(c) < 0x80 and (c.isalnum() or c in "'-$#%."):
            j = i
            while j < len(text) and ord(text[j]) < 0x80 and text[j] not in '\n' and (text[j].isalnum() or text[j] in "'-$#%.,:;!?\")"):
                j += 1
            while j < len(text) and text[j] == ' ':
                j += 1
            out.append(text[i:j]); i = j; continue
        out.append(c); i += 1
    return out

def wrap(text, font, width):
    lines = []
    for para in text.split('\n'):
        toks = tokens(para)
        cur = ''
        for t in toks:
            if not cur and t == ' ':
                continue
            if font.getlength(cur + t.rstrip(' ')) <= width or not cur:
                cur += t; continue
            # 行頭に来てはいけない字は前の行へ
            if t[0] in NO_START:
                cur += t; continue
            # 行末に来てはいけない字は次の行へ
            carry = ''
            while cur and cur[-1] in NO_END:
                carry = cur[-1] + carry; cur = cur[:-1]
            lines.append(cur.rstrip(' '))
            cur = carry + t.lstrip(' ')
        lines.append(cur.rstrip(' '))
    while lines and not lines[-1]:
        lines.pop()
    return lines

def norm(t):
    # 全角英数字は半角に（字体の都合）、"…" はそのまま
    out = []
    for ch in t:
        n = unicodedata.normalize('NFKC', ch)
        if len(n) == 1 and 0xff01 <= ord(ch) <= 0xff5e and ch not in '（）！？：；，．':
            out.append(n)
        else:
            out.append(ch)
    t = ''.join(out)
    import re
    t = re.sub(r'[Pp]aragraphs?\s*', 'パラグラフ ', t).replace('パラグラフ を', 'パラグラフを').replace('パラグラフ 」', 'パラグラフ」')
    return t

def render(text, font):
    lines = wrap(norm(text), font, WIDTH - LEFT * 2 - SIZE * 2)
    h = TOP + len(lines) * PITCH + 12
    h = (h + 19) // 20 * 20
    im = Image.new('L', (WIDTH, h), 0)
    dr = ImageDraw.Draw(im)
    for i, l in enumerate(lines):
        dr.text((LEFT, TOP + i * PITCH), l, fill=255, font=font)
    return im

def render_help(blocks, font):
    """見出しつきのヘルプ（能力値・技能の説明）"""
    hf = ImageFont.truetype(FONT, 24)
    try:
        hf.set_variation_by_axes([700])
    except Exception:
        pass
    items = []   # (font, 行, 行の高さ)
    for b in blocks:
        if 'h' in b:
            items.append((hf, [norm(b['h'])], 34))
        else:
            items.append((font, wrap(norm(b["t"]), font, WIDTH - LEFT * 2 - SIZE * 2), PITCH))
    h = TOP
    for f, ls, p in items:
        h += len(ls) * p + 8
    h = (h + 12 + 19) // 20 * 20
    im = Image.new('L', (WIDTH, h), 0)
    dr = ImageDraw.Draw(im)
    y = TOP
    for f, ls, p in items:
        for l in ls:
            dr.text((LEFT, y), l, fill=255, font=f); y += p
        y += 8
    return im

def main():
    png = '--png' in sys.argv
    d = open(SRC, 'rb').read()
    head, zeros, imgs, tail = parse(d)
    tr = {}
    for f in glob.glob(os.path.join(ROOT, 'work', 'out', 'p*.json')):
        tr.update(json.load(open(f, encoding='utf-8')))
    fix = os.path.join(ROOT, 'text', 'para_fix.json')
    if os.path.exists(fix):
        tr.update(json.load(open(fix, encoding='utf-8')))
    hp = os.path.join(ROOT, 'text', 'help_ja.json')
    helps = json.load(open(hp, encoding='utf-8')) if os.path.exists(hp) else {}
    font = load_font()
    if png: os.makedirs(os.path.join(ROOT, 'build', 'para_png'), exist_ok=True)
    out = []
    n = 0
    for i, raw in enumerate(imgs):
        t = tr.get(str(i))
        if i in (162, 163) and str(i) in helps:
            im = render_help(helps[str(i)], font)
        elif t is None or i >= 162:
            out.append(raw); continue
        else:
            im = render(t, font)
        if png: im.save(os.path.join(ROOT, 'build', 'para_png', 'p%03d.png' % i))
        w, h = im.size
        px = im.tobytes()
        px += bytes((-len(px)) & 3)
        out.append(struct.pack('<II', w, h) + px)
        n += 1
    blob = b''.join(head) + zeros + b''.join(out) + tail
    comp = lz4.block.compress(blob, mode='high_compression', compression=12, store_size=False)
    open(os.path.join(ROOT, 'build', 'para.lz'), 'wb').write(struct.pack('<I', len(blob)) + comp)
    print('translated', n, 'raw', len(blob), 'lz4', len(comp))

if __name__ == '__main__':
    main()
