/*
 * テスト用: ゲームのフォルダに wljp_test というファイルがあるときだけ動く（Steam が起動し直すので環境変数は使えない）。
 * 試験版（build.bat test、/DWLJP_TEST）でだけコンパイルする。配布版には入らない。
 * 共有メモリ "WLJP_TESTIO"（tools/win.py が使う）
 *   +0   magic, req, ack, frame, path[260]   画面（SDL の画面サーフェス）を BMP に書き出す
 *   +276 ereq, eack, ev[4]                   SDL_PushEvent で入力を入れる（ウインドウを前面に出さない）
 *   +300 gx, gy, gok, pad
 *   +316 mreq, mack, maddr, mlen, mpath[260] DOS のメモリを書き出す（wl_jp.c の testio_dump）
 * ウインドウのタイトルは "WLJP-TEST <pid>" にする（ほかのウインドウと取り違えないように）
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t magic; volatile int32_t req, ack, frame; char path[260];
    volatile int32_t ereq, eack; int32_t ev[4];
    volatile int32_t gx, gy, gok, pad;
    volatile int32_t mreq, mack; uint32_t maddr, mlen; char mpath[260];
} shm_t;
typedef struct { uint8_t r, g, b, unused; } sdl_color;
typedef struct { int ncolors; sdl_color *colors; } sdl_palette;
typedef struct {
    sdl_palette *palette; uint8_t bpp, Bpp, Rloss, Gloss, Bloss, Aloss, Rshift, Gshift, Bshift, Ashift;
    uint32_t Rmask, Gmask, Bmask, Amask;
} sdl_format;
typedef struct { uint32_t flags; sdl_format *format; int w, h; uint16_t pitch; void *pixels; } sdl_surface;

void testio_dump(uint32_t addr, uint32_t len, const char *path);   /* wl_jp.c */
static shm_t *g_shm;
int g_testmode;
static sdl_surface *volatile g_screen;
typedef void (__cdecl *upd_t)(void *, int, void *);
typedef int (__cdecl *flip_t)(void *);
typedef void (__cdecl *cap_t)(const char *, const char *);
static upd_t real_upd;
static flip_t real_flip;
static cap_t real_cap;

static void save_bmp(const sdl_surface *s, const char *path, int dolock);
static volatile LONG g_busy;
static DWORD g_req_tick;
static void frame_done(void *s)
{
    g_screen = (sdl_surface*)s;
    if (!g_shm) return;
    g_shm->frame++;
    if (g_shm->mreq != g_shm->mack) { int r = g_shm->mreq; testio_dump(g_shm->maddr, g_shm->mlen, g_shm->mpath); g_shm->mack = r; }
    if (g_shm->req != g_shm->ack && !InterlockedExchange(&g_busy, 1)) {
        int r = g_shm->req;
        save_bmp(g_screen, g_shm->path, 1);
        g_shm->ack = r;
        InterlockedExchange(&g_busy, 0);
    }
}
static void __cdecl hook_upd(void *s, int n, void *r) { real_upd(s, n, r); frame_done(s); }
static int __cdecl hook_flip(void *s) { int v = real_flip(s); frame_done(s); return v; }
static void __cdecl hook_cap(const char *t, const char *i)
{
    char b[64];
    (void)t;
    wsprintfA(b, "WLJP-TEST %u", GetCurrentProcessId());
    real_cap(b, i);
}

static void save_bmp(const sdl_surface *s, const char *path, int dolock)
{
    int w = s->w, h = s->h, x, y, stride = (w * 3 + 3) & ~3;
    const sdl_format *f = s->format;
    BITMAPFILEHEADER fh = {0}; BITMAPINFOHEADER ih = {0};
    uint8_t *row;
    FILE *o;
    static int (__cdecl *lock)(void *); static void (__cdecl *unlock)(void *);
    if (!lock) { HMODULE m = GetModuleHandleA("SDL.dll"); lock = (int (__cdecl*)(void*))GetProcAddress(m, "SDL_LockSurface"); unlock = (void (__cdecl*)(void*))GetProcAddress(m, "SDL_UnlockSurface"); }
    int (__cdecl *lk)(void *) = dolock ? lock : NULL; void (__cdecl *ulk)(void *) = dolock ? unlock : NULL;
    if (lk && lk((void*)s) < 0) return;
    if (!s->pixels || !f) { if (ulk) ulk((void*)s); return; }
    row = (uint8_t*)malloc(stride);
    o = fopen(path, "wb");
    if (!o || !row) { if (o) fclose(o); free(row); if (ulk) ulk((void*)s); return; }
    fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof ih; fh.bfSize = fh.bfOffBits + stride * h;
    ih.biSize = sizeof ih; ih.biWidth = w; ih.biHeight = -h; ih.biPlanes = 1; ih.biBitCount = 24;
    fwrite(&fh, sizeof fh, 1, o); fwrite(&ih, sizeof ih, 1, o);
    for (y = 0; y < h; y++) {
        const uint8_t *p = (const uint8_t*)s->pixels + y * s->pitch;
        memset(row, 0, stride);
        for (x = 0; x < w; x++) {
            uint32_t v; uint8_t r, g, b;
            if (f->Bpp == 1) {
                sdl_color c = f->palette->colors[p[x]]; r = c.r; g = c.g; b = c.b;
            } else {
                if (f->Bpp == 2) v = ((const uint16_t*)p)[x];
                else if (f->Bpp == 3) v = p[x * 3] | (p[x * 3 + 1] << 8) | (p[x * 3 + 2] << 16);
                else v = ((const uint32_t*)p)[x];
                r = (uint8_t)(((v & f->Rmask) >> f->Rshift) << f->Rloss);
                g = (uint8_t)(((v & f->Gmask) >> f->Gshift) << f->Gloss);
                b = (uint8_t)(((v & f->Bmask) >> f->Bshift) << f->Bloss);
            }
            row[x * 3] = b; row[x * 3 + 1] = g; row[x * 3 + 2] = r;
        }
        fwrite(row, 1, stride, o);
    }
    fclose(o); free(row);
    if (ulk) ulk((void*)s);
}

static int (__cdecl *sdl_push)(void *);
static void push_event(void)
{
    uint8_t e[24];
    int t = g_shm->ev[0];
    memset(e, 0, sizeof e);
    e[0] = (uint8_t)t;
    if (t == 2 || t == 3) {                  /* SDL_KeyboardEvent: type, which, state, keysym{scancode u8, sym i32 @4, mod @8, unicode u16 @12} */
        e[2] = t == 2 ? 1 : 0;
        e[4] = (uint8_t)g_shm->ev[2];
        *(int32_t*)(e + 8) = g_shm->ev[1];
        *(int32_t*)(e + 12) = g_shm->ev[3];   /* mod（shift = 1） */
    } else if (t == 4) {
        *(uint16_t*)(e + 4) = (uint16_t)g_shm->ev[2];
        *(uint16_t*)(e + 6) = (uint16_t)g_shm->ev[3];
    } else if (t == 5 || t == 6) {
        e[2] = (uint8_t)g_shm->ev[1];
        e[3] = t == 5 ? 1 : 0;
        *(uint16_t*)(e + 4) = (uint16_t)g_shm->ev[2];
        *(uint16_t*)(e + 6) = (uint16_t)g_shm->ev[3];
    }
    if (sdl_push) sdl_push(e);
}
static DWORD WINAPI shot_thread(LPVOID p)
{
    int exposed = 0;
    (void)p;
    for (;;) {
        Sleep(30);
        if (g_shm->ereq != g_shm->eack) { int r = g_shm->ereq; push_event(); g_shm->eack = r; }
        if (g_shm->req == g_shm->ack && g_shm->mreq == g_shm->mack) { g_req_tick = 0; exposed = 0; continue; }
        if (!g_req_tick) { g_req_tick = GetTickCount(); continue; }
        if (GetTickCount() - g_req_tick < 300) continue;
        if (!exposed && sdl_push) {
            uint8_t e[24]; memset(e, 0, sizeof e); e[0] = 17; sdl_push(e); exposed = 1; continue;
        }
        if (GetTickCount() - g_req_tick < 1500) continue;
        if (g_shm->mreq != g_shm->mack) { int r = g_shm->mreq; testio_dump(g_shm->maddr, g_shm->mlen, g_shm->mpath); g_shm->mack = r; }
        if (g_shm->req != g_shm->ack && g_screen && !InterlockedExchange(&g_busy, 1)) {
            int r = g_shm->req;
            if (r != g_shm->ack) { save_bmp(g_screen, g_shm->path, 0); g_shm->ack = r; }
            InterlockedExchange(&g_busy, 0);
        }
    }
}

/* wl_jp.c */
void patch_iat(HMODULE m, const char *dll, const char *fn, void *hook, void **orig);

void testio_init(void)
{
    char f[MAX_PATH], *s;
    HANDLE h;
    GetModuleFileNameA(NULL, f, MAX_PATH);
    s = strrchr(f, '\\');
    if (s) lstrcpyA(s + 1, "wljp_test");
    if (GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES) return;
    g_testmode = 1;
    h = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(shm_t), "WLJP_TESTIO");
    if (!h) return;
    g_shm = (shm_t*)MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(shm_t));
    if (!g_shm) return;
    g_shm->magic = 0x54534554;
    g_shm->req = g_shm->ack = 0;
    g_shm->mreq = g_shm->mack = 0;
    g_shm->ereq = g_shm->eack = 0;
    patch_iat(GetModuleHandleA(NULL), "SDL.dll", "SDL_UpdateRects", (void*)hook_upd, (void**)&real_upd);
    patch_iat(GetModuleHandleA(NULL), "SDL.dll", "SDL_Flip", (void*)hook_flip, (void**)&real_flip);
    patch_iat(GetModuleHandleA(NULL), "SDL.dll", "SDL_WM_SetCaption", (void*)hook_cap, (void**)&real_cap);
    sdl_push = (int (__cdecl*)(void*))GetProcAddress(GetModuleHandleA("SDL.dll"), "SDL_PushEvent");
    CreateThread(NULL, 0, shot_thread, NULL, 0, NULL);
}
