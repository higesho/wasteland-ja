# 制御の字 <-> 訳者向けの記号
import re
CODES = {0x0d: '\n', 0x0b: '{NAME}', 0x0f: '{NUM}', 0x10: '{ITEM}', 0x11: '{MENU}', 0x06: '{WAIT}',
         0x07: '{C7}', 0x04: '{C4}', 0x03: '{C3}', 0x08: '{C8}', 0x01: '{HI}', 0x02: '{/HI}',
         0x0a: '{PL}', 0x0c: '{SEX}', 0x0e: '{PRON}', 0x7f: '{7F}', 0x09: '{TAB}'}
REV = {v: k for k, v in CODES.items() if v != '\n'}

def to_tok(s):
    out = []
    for ch in s:
        o = ord(ch)
        if o in CODES: out.append(CODES[o])
        elif o < 0x20 or o >= 0x7f: out.append('{X%02X}' % o)
        else: out.append(ch)
    return ''.join(out)

def from_tok(t):
    def rep(m):
        k = m.group(0)
        if k in REV: return chr(REV[k])
        if k.startswith('{X'): return chr(int(k[2:4], 16))
        return k
    t = t.replace('\r\n', '\n').replace('\n', '\r')
    return re.sub(r'\{(?:NAME|NUM|ITEM|MENU|WAIT|C7|C4|C3|C8|HI|/HI|PL|SEX|PRON|7F|TAB|X[0-9A-F]{2})\}', rep, t)

TOKRE = re.compile(r'\{(?:NAME|NUM|ITEM|MENU|WAIT|C7|C4|C3|C8|HI|/HI|PL|SEX|PRON|7F|TAB|X[0-9A-F]{2})\}')
