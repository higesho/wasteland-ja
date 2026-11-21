# 展開した WL.EXE（work/WL_unp.exe）を調べる補助（16 ビット）
import struct, re, capstone, sys
D0 = open('L:/project4/wasteland-ja/work/WL_unp.exe', 'rb').read()
HP = struct.unpack_from('<H', D0, 8)[0] * 16
IMG = D0[HP:]
NREL = struct.unpack_from('<H', D0, 6)[0]
RELS = [struct.unpack_from('<HH', D0, 0x1c + 4 * i) for i in range(NREL)]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
def lin(seg, off): return seg * 16 + off
def dis(seg, off, n=40, out=True):
    a = lin(seg, off); res = []
    for i, ins in enumerate(md.disasm(IMG[a:a + n * 7], off)):
        if i >= n: break
        res.append(ins)
        if out: print('%04x:%04x  %-16s %s %s' % (seg, ins.address, IMG[lin(seg, ins.address):lin(seg, ins.address) + ins.size].hex(), ins.mnemonic, ins.op_str))
    return res
def segrefs():
    return sorted(set(struct.unpack_from('<H', IMG, s * 16 + o)[0] for o, s in RELS))
if __name__ == '__main__':
    exec(sys.argv[1])
