# テスト用の Wasteland を動かす。ウインドウは固有のタイトル "WLJP-TEST <pid>" で探す（前面のウインドウは使わない）
#   python src/tools/win.py start [en]     DLL を winmm.dll としてゲームのフォルダに置いて起動（en: DLL なし = 英語）
#   python src/tools/win.py shot NAME      画面（SDL の画面サーフェス）を capture/NAME.png に
#   python src/tools/win.py key KEY ...    キー（enter esc space up down left right f1.. a..z 0..9, text:abc, wait:秒, S-x = shift+x）
#   python src/tools/win.py dump ADDR LEN FILE   DOS のメモリを書き出す
#   python src/tools/win.py title | stop
import sys, os, time, ctypes, ctypes.wintypes as W, subprocess, shutil, struct, mmap
from ctypes import windll
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
GAME = r'L:\SteamLibrary\steamapps\common\Wasteland'
PIDF = os.path.join(ROOT, 'test', 'pid.txt')
u32 = windll.user32
WNDENUMPROC = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)


def find_window(pid=None):
    # Steam が起動し直すので PID は窓のタイトル "WLJP-TEST <pid>" から取る
    found = []
    def cb(h, l):
        n = u32.GetWindowTextLengthW(h)
        b = ctypes.create_unicode_buffer(n + 1)
        u32.GetWindowTextW(h, b, n + 1)
        if b.value.startswith('WLJP-TEST '):
            p = W.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(p))
            if b.value == 'WLJP-TEST %d' % p.value and (pid is None or p.value == pid):
                found.append((h, b.value, p.value))
        return True
    u32.EnumWindows(WNDENUMPROC(cb), 0)
    if not found:
        raise SystemExit('WLJP-TEST window not found')
    return found[0]


def _shm():
    m = mmap.mmap(-1, 592, tagname='WLJP_TESTIO')
    if struct.unpack_from('<I', m, 0)[0] != 0x54534554: raise SystemExit('test DLL not running')
    return m


def shot(name, scale=None):
    from PIL import Image
    m = _shm()
    bmp = os.path.join(ROOT, 'test', 'shot.bmp')
    if os.path.exists(bmp): os.remove(bmp)
    p = bmp.encode('mbcs') + bytes(1); m[16:16 + len(p)] = p
    req = struct.unpack_from('<i', m, 4)[0] + 1; struct.pack_into('<i', m, 4, req)
    t = time.time()
    while struct.unpack_from('<i', m, 8)[0] != req:
        if time.time() - t > 10: raise SystemExit('shot timeout')
        time.sleep(0.05)
    im = Image.open(bmp).convert('RGB')
    out = os.path.join(ROOT, 'capture', name + '.png')
    im.save(out)
    print(out, im.size)


def dump(addr, ln, path):
    m = _shm()
    p = os.path.abspath(path).encode('mbcs') + bytes(1)
    struct.pack_into('<II', m, 324, addr, ln); m[332:332 + len(p)] = p
    req = struct.unpack_from('<i', m, 316)[0] + 1; struct.pack_into('<i', m, 316, req)
    t = time.time()
    while struct.unpack_from('<i', m, 320)[0] != req:
        if time.time() - t > 10: raise SystemExit('dump timeout')
        time.sleep(0.05)
    print(path)


def poke(addr, hexs):
    path = os.path.join(ROOT, 'test', 'poke.bin')
    data = bytes.fromhex(hexs)
    open(path, 'wb').write(data)
    dump(addr | 0x80000000, len(data), path)


VK = {'enter': (13, 0x1c), 'esc': (27, 0x01), 'space': (32, 0x39), 'tab': (9, 0x0f), 'back': (8, 0x0e),
      'up': (273, 0x48), 'down': (274, 0x50), 'right': (275, 0x4d), 'left': (276, 0x4b),
      'home': (278, 0x47), 'end': (279, 0x4f), 'pgup': (280, 0x49), 'pgdn': (281, 0x51),
      'minus': (45, 0x0c), 'period': (46, 0x34), 'slash': (47, 0x35), 'comma': (44, 0x33),
      'quote': (39, 0x28), 'semicolon': (59, 0x27), 'equals': (61, 0x0d), 'shift': (304, 0x2a)}
for i in range(1, 13):
    VK['f%d' % i] = (281 + i, 0x3a + i if i <= 10 else 0x57 + i - 11)
ROWS = ['1234567890', 'qwertyuiop', 'asdfghjkl', 'zxcvbnm']
for r, s0 in zip(ROWS, [0x02, 0x10, 0x1e, 0x2c]):
    for k, ch in enumerate(r):
        VK[ch] = (ord(ch), s0 + k)


def send_ev(m, t, a=0, b=0, c=0):
    struct.pack_into('<4i', m, 284, t, a, b, c)
    r = struct.unpack_from('<i', m, 276)[0] + 1
    struct.pack_into('<i', m, 276, r)
    t0 = time.time()
    while struct.unpack_from('<i', m, 280)[0] != r:
        if time.time() - t0 > 5: raise SystemExit('event timeout')
        time.sleep(0.01)


def post_key(m, name, hold=0.15):
    shift = name.startswith('S-')
    if shift:
        name = name[2:]
        send_ev(m, 2, *VK['shift'], 1); time.sleep(0.05)
    sym, sc = VK[name]
    send_ev(m, 2, sym, sc, 1 if shift else 0)
    time.sleep(hold)
    send_ev(m, 3, sym, sc, 1 if shift else 0)
    if shift:
        time.sleep(0.05); send_ev(m, 3, *VK['shift'], 0)
    time.sleep(0.12)


def key(names):
    m = _shm()
    for n in names:
        if n.startswith('text:'):
            for ch in n[5:]:
                if ch == ' ': post_key(m, 'space')
                elif ch.isupper(): post_key(m, 'S-' + ch.lower())
                else: post_key(m, ch)
        elif n.startswith('wait:'):
            time.sleep(float(n[5:]))
        elif n.startswith('click:'):
            x, y = [int(v) for v in n[6:].split(',')]
            send_ev(m, 4, 0, x, y); time.sleep(0.1)
            send_ev(m, 5, 1, x, y); time.sleep(0.12)
            send_ev(m, 6, 1, x, y); time.sleep(0.15)
        else:
            post_key(m, n if n.startswith('S-') else n.lower())


def start(variant):
    dst = os.path.join(GAME, 'winmm.dll')
    if os.path.exists(dst):
        os.remove(dst)
    if variant != 'en':
        shutil.copy(os.path.join(ROOT, 'build', 'winmm.dll'), dst)
    with open(os.path.join(GAME, 'steam_appid.txt'), 'w') as f:
        f.write('240760')
    # テスト機能・ログはゲームのフォルダのフラグファイルで入れる（Steam が起動し直すので環境変数は届かない）
    for f in ('wljp_test', 'wljp_log'):
        open(os.path.join(GAME, f), 'w').close()
    subprocess.Popen([os.path.join(GAME, 'wasteland.exe')], cwd=GAME)
    for i in range(80):
        time.sleep(0.25)
        try:
            h, t, pid = find_window()
            open(PIDF, 'w').write(str(pid))
            print('pid', pid, 'window', t)
            return
        except SystemExit:
            pass
    print('window not found yet')


def stop():
    try:
        pid = int(open(PIDF).read())
        subprocess.call(['taskkill', '/PID', str(pid), '/F'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except Exception as e:
        print(e)
    time.sleep(1)


if __name__ == '__main__':
    c = sys.argv[1]
    if c == 'start': start(sys.argv[2] if len(sys.argv) > 2 else 'jp')
    elif c == 'shot': shot(sys.argv[2])
    elif c == 'key': key(sys.argv[2:])
    elif c == 'dump': dump(int(sys.argv[2], 0), int(sys.argv[3], 0), sys.argv[4])
    elif c == 'poke': poke(int(sys.argv[2], 0), sys.argv[3])
    elif c == 'title': print(find_window())
    elif c == 'shotonly': pass
    elif c == 'stop': stop()
