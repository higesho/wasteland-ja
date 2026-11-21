# ダンプ（work/mem.bin, 0x400000 から）を調べる補助
import sys, struct, re, capstone
D=open('L:/project4/wasteland-ja/work/mem.bin','rb').read()
B=0x400000
md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32)
def s(a): # string at VA
    o=a-B; e=D.index(b'\0',o); return D[o:e]
def find(bs):
    return [m.start()+B for m in re.finditer(re.escape(bs),D)]
def xref(va, lo=0x401000, hi=0x5b1000):
    pat=struct.pack('<I',va)
    return [m.start()+lo for m in re.finditer(re.escape(pat),D[lo-B:hi-B])]
def dis(va,n=40):
    o=va-B
    for i,ins in enumerate(md.disasm(D[o:o+n*8],va)):
        if i>=n: break
        print('%08x: %-24s %s %s'%(ins.address,D[ins.address-B:ins.address-B+ins.size].hex(),ins.mnemonic,ins.op_str))
def func_start(va):
    o=va-B
    while o>0x1000:
        if D[o-1] in (0xcc,0xc3) and D[o]==0x55 and D[o+1:o+3]==b'\x8b\xec': return o+B
        o-=1
if __name__=='__main__':
    exec(sys.argv[1])
