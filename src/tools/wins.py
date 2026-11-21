import ctypes,sys
from ctypes import wintypes as W
u=ctypes.WinDLL('user32')
pid=int(sys.argv[1])
res=[]
@ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
def cb(h,l):
    p=W.DWORD(); u.GetWindowThreadProcessId(h,ctypes.byref(p))
    if p.value==pid:
        b=ctypes.create_unicode_buffer(256); u.GetWindowTextW(h,b,256)
        r=W.RECT(); u.GetWindowRect(h,ctypes.byref(r))
        res.append((h,b.value,u.IsWindowVisible(h),(r.left,r.top,r.right,r.bottom)))
    return True
u.EnumWindows(cb,0)
for r in res: print(r)
