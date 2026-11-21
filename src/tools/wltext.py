# Wasteland の文字列の読み書き（5 ビットの文字表つき圧縮文字列、GAME1/GAME2 の msq ブロック）
import struct

def decode_group_strings(buf, S, count=None):
    """S = 文字表（60 バイト）の位置。戻り値: [(番号, 文字列(bytes), 始まりの位置)]"""
    table = buf[S:S + 60]
    first = struct.unpack_from('<H', buf, S + 60)[0]
    ngroups = first // 2
    out = []
    for g in range(ngroups):
        p = S + 60 + struct.unpack_from('<H', buf, S + 60 + 2 * g)[0]
        bitpos = 0
        for k in range(4):
            n = g * 4 + k
            if count is not None and n >= count:
                return out
            s, p, bitpos = decode_one(buf, p, bitpos, table)
            out.append((n, s))
    return out

def decode_one(buf, p, bitpos, table):
    s = bytearray()
    upper = high = False
    while True:
        code = 0
        for i in range(5):
            if p >= len(buf):
                return bytes(s), p, bitpos
            b = (buf[p] >> bitpos) & 1
            code |= b << i
            bitpos += 1
            if bitpos == 8:
                bitpos = 0; p += 1
        if code == 0x1f:
            high = True; continue
        if code == 0x1e:
            upper = True; continue
        ch = table[code + (30 if high else 0)]
        high = False
        if ch == 0:
            return bytes(s), p, bitpos
        if upper and 0x61 <= ch <= 0x7a:
            ch -= 0x20
        upper = False
        s.append(ch)

# EXE（展開済みの像。wastelib の位置 - 0x200）の文字表つき文字列表
EXE_TABLES = [('intro', 0x17723, 18), ('message', 0x17B5E, 107), ('inventory', 0x18290, 170),
              ('chargen', 0x19E6B, 8), ('ending', 0x1A1AE, 10), ('promotion', 0x1A642, 63),
              ('library', 0x1AAEC, 11), ('shop', 0x1AC18, 10), ('infirmary', 0x1AD0D, 25)]

def msq_blocks(data):
    disk = data[3]
    pos = []
    i = 0
    while True:
        j = data.find(b'msq' + bytes([disk]), i)
        if j < 0: break
        pos.append(j); i = j + 4
    return [(pos[k], (pos[k + 1] if k + 1 < len(pos) else len(data))) for k in range(len(pos))]

def decrypt(block):
    e1, e2 = block[4], block[5]
    key = e1 ^ e2
    out = bytearray()
    for c in block[6:]:
        out.append(c ^ key)
        key = (key + 0x1f) & 0xff
    return out

def map_info(body):
    for ms in (32, 64):
        cd = ms * ms * 3 // 2
        if cd + 0x2c + 3 < len(body) and body[cd + 0x2c] == ms and body[cd + 0x2a] == 0 and body[cd + 0x2b] == 0:
            return ms, cd
    return None, None
