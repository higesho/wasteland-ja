# プロセスのメモリ（wasteland.exe のイメージ）を取り出す: procdump.py PID OUT
import ctypes, sys
from ctypes import wintypes as W
k=ctypes.WinDLL('kernel32',use_last_error=True)
pid=int(sys.argv[1])
h=k.OpenProcess(0x0410,False,pid)
base=0x400000; size=0x1f9d000
buf=ctypes.create_string_buffer(size)
out=bytearray(size)
class MBI(ctypes.Structure):
    _fields_=[('BaseAddress',ctypes.c_void_p),('AllocationBase',ctypes.c_void_p),('AllocationProtect',W.DWORD),('RegionSize',ctypes.c_size_t),('State',W.DWORD),('Protect',W.DWORD),('Type',W.DWORD)]
a=base
while a<base+size:
    m=MBI(); k.VirtualQueryEx(h,ctypes.c_void_p(a),ctypes.byref(m),ctypes.sizeof(m))
    n=min(m.RegionSize, base+size-a)
    if m.State==0x1000:
        b=ctypes.create_string_buffer(n); r=ctypes.c_size_t()
        if k.ReadProcessMemory(h,ctypes.c_void_p(a),b,n,ctypes.byref(r)): out[a-base:a-base+n]=b.raw[:r.value]
    a+=n
open(sys.argv[2],'wb').write(out)
print('ok')
