# 固有のウインドウタイトル（と PID）でウインドウを探して PrintWindow で取得: shot.py TITLE OUT.png [pid]
import ctypes,sys
from ctypes import wintypes as W
from PIL import Image
u=ctypes.WinDLL('user32'); g=ctypes.WinDLL('gdi32')
title=sys.argv[1]; out=sys.argv[2]; pid=int(sys.argv[3]) if len(sys.argv)>3 else 0
found=[]
@ctypes.WINFUNCTYPE(W.BOOL,W.HWND,W.LPARAM)
def cb(h,l):
    b=ctypes.create_unicode_buffer(256); u.GetWindowTextW(h,b,256)
    p=W.DWORD(); u.GetWindowThreadProcessId(h,ctypes.byref(p))
    if b.value==title and (not pid or p.value==pid): found.append(h)
    return True
u.EnumWindows(cb,0)
if not found: sys.exit('no window')
h=found[0]
r=W.RECT(); u.GetClientRect(h,ctypes.byref(r)); w,hh=r.right,r.bottom
dc=u.GetDC(h); mdc=g.CreateCompatibleDC(dc); bm=g.CreateCompatibleBitmap(dc,w,hh); g.SelectObject(mdc,bm)
u.PrintWindow(h,mdc,3)
class BIH(ctypes.Structure):
    _fields_=[('a',W.DWORD),('w',W.LONG),('h',W.LONG),('p',W.WORD),('b',W.WORD),('c',W.DWORD),('s',W.DWORD),('x',W.LONG),('y',W.LONG),('u',W.DWORD),('i',W.DWORD)]
bi=BIH(40,w,-hh,1,32,0,0,0,0,0,0)
buf=ctypes.create_string_buffer(w*hh*4)
g.GetDIBits(mdc,bm,0,hh,buf,ctypes.byref(bi),0)
Image.frombuffer('RGBA',(w,hh),buf,'raw','BGRA',0,1).convert('RGB').save(out)
g.DeleteObject(bm); g.DeleteDC(mdc); u.ReleaseDC(h,dc)
print(out,w,hh)
