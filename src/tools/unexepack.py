# EXEPACK を外す: unexepack.py IN OUT
import struct, sys
d = open(sys.argv[1], 'rb').read()
hdr = struct.unpack_from('<14H', d, 0)
cblp, cp, nrel, hpar, minal, maxal, ss, sp, csum, ip, cs, relo = hdr[1:13]
hsize = hpar * 16
img = d[hsize:]
eh = cs * 16
rip, rcs, _, xsize, rsp, rss, dlen = struct.unpack_from('<7H', img, eh)
sig_off = eh + 16 if img[eh+14:eh+16] != b'RB' else eh + 14
assert img[sig_off:sig_off+2] == b'RB' or img[eh+14:eh+16] == b'RB', img[eh:eh+20]
skip = struct.unpack_from('<H', img, eh + 14)[0] if img[eh+16:eh+18] == b'RB' else 1
buf = bytearray(img[:eh]) + bytearray(dlen * 16 - eh if dlen * 16 > eh else 0)
src = eh - 1
while img[src] == 0xff: src -= 1
dst = dlen * 16 - 1 if dlen * 16 > eh else eh - 1
while True:
    c = img[src]; src -= 1
    n = (img[src] << 8) | img[src - 1]; src -= 2
    if c & 0xfe == 0xb0:
        b = img[src]; src -= 1
        for k in range(n): buf[dst] = b; dst -= 1
    elif c & 0xfe == 0xb2:
        for k in range(n): buf[dst] = img[src]; dst -= 1; src -= 1
    else:
        raise SystemExit('bad cmd %x' % c)
    if c & 1: break
# relocations after the stub code ("Packed file is corrupt" message)
stub = img[eh:eh + xsize]
p = stub.index(b'Packed file is corrupt') + len(b'Packed file is corrupt')
rel = []
for seg in range(16):
    n = struct.unpack_from('<H', stub, p)[0]; p += 2
    for k in range(n):
        rel.append((struct.unpack_from('<H', stub, p)[0], seg * 0x1000)); p += 2
body = bytes(buf)
hp = (0x1c + 4 * len(rel) + 15) // 16
out = bytearray(hp * 16)
total = hp * 16 + len(body)
minal2 = max(0, minal - ((len(body) - eh) + 15) // 16) if False else minal
struct.pack_into('<14H', out, 0, 0x5a4d, total % 512, (total + 511) // 512, len(rel), hp, minal, maxal, rss, rsp, 0, rip, rcs, 0x1c, 0)
for i, (o, s) in enumerate(rel): struct.pack_into('<HH', out, 0x1c + 4 * i, o, s)
open(sys.argv[2], 'wb').write(out + body)
print('ok entry %04x:%04x ss:sp %04x:%04x img %x relocs %d minalloc %x' % (rcs, rip, rss, rsp, len(body), len(rel), minal))
