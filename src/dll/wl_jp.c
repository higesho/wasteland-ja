/*
 * Wasteland（1988, Steam 版 Wasteland 1 - The Original Classic）日本語化パッチ（実行時部分）
 * wasteland.exe（inXile の DOSBox 0.74 改造版、MSVC 2010）に winmm.dll の代理として読み込まれる。
 * ゲームのファイルは書き換えない。DOSBox が開くファイルを差し替える（fopen）:
 *  - rom\WL.exe   → EXEPACK を外し、文字列を出す所にコールバック（FE 38 nn 00）を置いたもの
 *  - rom\data\WLA.BIN（EGA ドライバ）→ 1 字描く所にコールバックを置いたもの
 *  - paragraphs.bin → 日本語のパラグラフ画像（inXile の高解像度表示用）
 * 日本語の字: 文字列を出す時に原文全体を引き、訳文を 1 字ずつ流す。字は「使い回し枠」の 1 バイト（0xA0-0xFF）で
 * 流し、ドライバで字形を差し替える。1 字 = 1 セル（8x8、美咲フォント）
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "resource.h"

void proxy_init(void);
void testio_init(void);
void patch_iat(HMODULE m, const char *dll, const char *fn, void *hook, void **orig);
extern int g_testmode;

static char g_dir[MAX_PATH];
static char g_cache[MAX_PATH];
static FILE *g_log;
static int g_logon;
static void dbg(const char *fmt, ...)
{
    va_list ap;
    if (!g_logon) return;
    if (!g_log) {
        char p[MAX_PATH];
        wsprintfA(p, "%s\\wljp_log.txt", g_dir);
        g_log = fopen(p, "a");
        if (!g_log) return;
    }
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

/* ------------------------------------------------ DOSBox（wasteland.exe）の中身
 * Steam 版 wasteland.exe（md5 89834ed0..）。SteamStub で .text が暗号化されているので、
 * 確かめるのは起動の後（SDL_SetVideoMode か fopen の時、復号済み）にする */
#define A_CB_HANDLERS 0x6085d8u      /* CallBack_Handlers[128] */
#define A_CB_ILLEGAL  0x424440u      /* IllegalHandler */
#define A_SEGVAL      0x62f430u      /* Segs.val[es,cs,ss,ds,fs,gs] */
#define A_SEGPHYS     0x62f450u
#define A_REGS        0x62f470u      /* cpu_regs.regs[8], ip */
#define A_TLB_READ    0x62f6a0u
#define A_TLB_WRITE   0xa2f6a0u
#define A_TLB_RH      0xe2f6a0u
#define A_TLB_WH      0x122f6a0u
static int g_dbx_ok = -1;
static int verify_dosbox(void)
{
    /* CALLBACK_Allocate: cmp [eax*4+CB], ecx / CALLBACK_DeAllocate: mov [eax*4+CB], IllegalHandler
     * CALLBACK_RunRealFar: mov [Segs.val+4], 0f000h / mem_readb: shr eax,0ch; mov eax,[eax*4+TLB_RH] */
    static const uint8_t s1[] = {0x39, 0x0c, 0x85, 0xd8, 0x85, 0x60, 0x00};
    static const uint8_t s2[] = {0xc7, 0x04, 0x85, 0xd8, 0x85, 0x60, 0x00, 0x40, 0x44, 0x42, 0x00};
    static const uint8_t s3[] = {0xc7, 0x05, 0x34, 0xf4, 0x62, 0x00, 0x00, 0xf0, 0x00, 0x00};
    static const uint8_t s4[] = {0xc1, 0xe8, 0x0c, 0x8b, 0x04, 0x85, 0xa0, 0xf6, 0xe2, 0x00};
    MEMORY_BASIC_INFORMATION mi;
    if ((uintptr_t)GetModuleHandleA(NULL) != 0x400000) return 0;
    if (!VirtualQuery((void*)0x425c70, &mi, sizeof mi) || mi.State != MEM_COMMIT) return 0;
    if (memcmp((void*)0x424470, s1, sizeof s1)) return 0;
    if (memcmp((void*)0x4244a4, s2, sizeof s2)) return 0;
    if (memcmp((void*)0x4244d9, s3, sizeof s3)) return 0;
    if (memcmp((void*)0x425c70, s4, sizeof s4)) return 0;
    return 1;
}
static int dbx_ok(void)
{
    if (g_dbx_ok < 0) { g_dbx_ok = verify_dosbox(); dbg("verify_dosbox %d\n", g_dbx_ok); }
    return g_dbx_ok;
}
/* PageHandler の仮想関数（MSVC, thiscall）: [1] readb, [4] writeb */
typedef uint32_t (__fastcall *ph_rb)(void *self, void *edx, uint32_t a);
typedef void (__fastcall *ph_wb)(void *self, void *edx, uint32_t a, uint32_t v);
static __inline uint8_t rb(uint32_t a)
{
    uint8_t *p = ((uint8_t**)A_TLB_READ)[a >> 12];
    void *h;
    if (p) return p[a];
    h = ((void**)A_TLB_RH)[a >> 12];
    return (uint8_t)((ph_rb)((*(void***)h)[1]))(h, NULL, a);
}
static __inline void wb(uint32_t a, uint8_t v)
{
    uint8_t *p = ((uint8_t**)A_TLB_WRITE)[a >> 12];
    void *h;
    if (p) { p[a] = v; return; }
    h = ((void**)A_TLB_WH)[a >> 12];
    ((ph_wb)((*(void***)h)[4]))(h, NULL, a, v);
}
static __inline uint16_t rw(uint32_t a) { return (uint16_t)(rb(a) | (rb(a + 1) << 8)); }
static __inline void ww(uint32_t a, uint16_t v) { wb(a, (uint8_t)v); wb(a + 1, (uint8_t)(v >> 8)); }
#define REG32(i) (((uint32_t*)A_REGS)[i])
#define R_AX ((uint16_t)REG32(0))
#define R_CX ((uint16_t)REG32(1))
#define R_DX ((uint16_t)REG32(2))
#define R_BX ((uint16_t)REG32(3))
#define R_SP ((uint16_t)REG32(4))
#define R_BP ((uint16_t)REG32(5))
#define R_SI ((uint16_t)REG32(6))
#define R_DI ((uint16_t)REG32(7))
#define R_IP ((uint16_t)REG32(8))
#define SET16(i, v) (*(uint16_t*)&REG32(i) = (uint16_t)(v))
#define SET_AL(v) (*(uint8_t*)&REG32(0) = (uint8_t)(v))
#define SEGV(i) (((uint32_t*)A_SEGVAL)[i])
#define PHYS(i) (((uint32_t*)A_SEGPHYS)[i])
#define PHYS_ES PHYS(0)
#define PHYS_CS PHYS(1)
#define PHYS_SS PHYS(2)
#define PHYS_DS PHYS(3)
static void push16(uint16_t v) { SET16(4, R_SP - 2); ww(PHYS_SS + R_SP, v); }

/* テスト: DOS のメモリを書き出す */
void testio_dump(uint32_t addr, uint32_t len, const char *path)
{
    FILE *o;
    uint8_t *b;
    uint32_t i;
    if (addr & 0x80000000u) {         /* テスト: このプロセスのメモリに書く（中身はファイルから） */
        FILE *f = fopen(path, "rb");
        if (f) { uint8_t t[256]; size_t n = fread(t, 1, len < 256 ? len : 256, f); fclose(f);
                 memcpy((void*)(uintptr_t)(addr & 0x7fffffffu), t, n); dbg("poke %x %u\n", addr & 0x7fffffffu, (unsigned)n); }
        return;
    }
    if (g_dbx_ok != 1 || len > 0x200000) return;
    b = (uint8_t*)malloc(len);
    if (!b) return;
    for (i = 0; i < len; i++) b[i] = (addr + i >= 0xa0000 && addr + i < 0xc0000) ? 0 : rb(addr + i);
    o = fopen(path, "wb");
    if (o) { fwrite(b, 1, len, o); fclose(o); }
    free(b);
    dbg("dump %x %x cs=%04x ds=%04x\n", addr, len, SEGV(1), SEGV(3));
}

/* ------------------------------------------------------- パッチデータ（RCDATA 101）
 * 'WLJ1', 節の数, 節 {u8 種類, u32 長さ, 中身}
 *  'G' 字形: u32 件数, {u8 字形[8]}（番号 = 位置）
 *  'K' 禁則: u8[件数]（1 = 行頭に置かない、2 = 行末に置かない）
 *  'T' 訳: u32 件数, {u32 原文の FNV-1a, u32 位置}（原文の値で並ぶ）、続いて中身 {u16 長さ, u16 単位[長さ]}
 *      単位: 0x00-0xFF はそのままのバイト、0x100 以上は字形の番号 + 0x100
 *  'P' paragraphs.bin（zlib でなく生のまま。build 時に LZ 圧縮したもの）: u32 元の大きさ, 圧縮データ
 */
static const uint8_t *g_blob;
static uint32_t g_blob_len;
static const uint8_t *g_glyph;      /* [n][8] */
static uint32_t g_nglyph;
static const uint8_t *g_kinsoku;
static const uint8_t *g_tr_idx, *g_tr_data;
static uint32_t g_ntr;
static const uint8_t *g_para;
static uint32_t g_para_len;
static int load_blob(HMODULE self)
{
    HRSRC r = FindResourceA(self, MAKEINTRESOURCEA(IDR_PATCH), (LPCSTR)RT_RCDATA);
    HGLOBAL g;
    const uint8_t *p, *end;
    uint32_t ns, i;
    if (!r) return 0;
    g = LoadResource(self, r);
    g_blob = (const uint8_t*)LockResource(g);
    g_blob_len = SizeofResource(self, r);
    if (!g_blob || g_blob_len < 8 || memcmp(g_blob, "WLJ1", 4)) return 0;
    ns = *(const uint32_t*)(g_blob + 4);
    p = g_blob + 8; end = g_blob + g_blob_len;
    for (i = 0; i < ns && p + 5 <= end; i++) {
        uint8_t t = p[0];
        uint32_t len = *(const uint32_t*)(p + 1);
        const uint8_t *d = p + 5;
        if (t == 'G') { g_nglyph = *(const uint32_t*)d; g_glyph = d + 4; }
        else if (t == 'K') g_kinsoku = d;
        else if (t == 'T') { g_ntr = *(const uint32_t*)d; g_tr_idx = d + 4; g_tr_data = d + 4 + g_ntr * 8; }
        else if (t == 'P') { g_para = d; g_para_len = len; }
        p = d + len;
    }
    dbg("blob glyphs %u tr %u para %u\n", g_nglyph, g_ntr, g_para_len);
    return g_glyph && g_tr_idx;
}
static uint32_t fnv(const uint8_t *s, int n)
{
    uint32_t h = 0x811c9dc5u;
    while (n--) { h ^= *s++; h *= 0x01000193u; }
    return h;
}
static const uint16_t *find_tr(const uint8_t *s, int n, int *len)
{
    uint32_t h = fnv(s, n), lo = 0, hi = g_ntr;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2, v = *(const uint32_t*)(g_tr_idx + m * 8);
        if (v < h) lo = m + 1;
        else if (v > h) hi = m;
        else {
            const uint8_t *d = g_tr_data + *(const uint32_t*)(g_tr_idx + m * 8 + 4);
            *len = *(const uint16_t*)d;
            return (const uint16_t*)(d + 2);
        }
    }
    return NULL;
}

/* ------------------------------------------------------------ 使い回し枠（字の 1 バイト符号 0xA0-0xFF） */
#define NSLOT 96
static uint16_t g_slot[NSLOT];       /* 枠 → 字形の番号 */
static int g_slot_next;
static uint8_t slot_alloc(uint16_t glyph)
{
    int s = g_slot_next;
    g_slot_next = (g_slot_next + 1) % NSLOT;
    g_slot[s] = glyph;
    return (uint8_t)(0xa0 + s);
}

/* ------------------------------------------------------------ WL.EXE の中の番地（コードの段 = CS） */
#define WL_PUTC      0x789c          /* 1 字出す（jmp [0b265h]） */
#define WL_PLAIN_CB  0x787a          /* 平文の文字列を出す繰り返し */
#define WL_PLAIN_END 0x787e          /* → jmp 788fh（[4680h]++ して戻る） */
#define WL_CSTR_CB   0x78a9          /* 圧縮文字列を出す繰り返し */
#define WL_HLOAD_RET 0x7a00          /* 履歴を描き直す所: call 0fh の次 */
#define DV_PTR       0x4680          /* 文字列の位置 */
#define DV_TABLE     0x46a1          /* 文字表 */
#define DV_BITS      0x46bb          /* 残りビット数（負なら次のバイトを読む） */
#define DV_FLAGS     0x46bc          /* 大文字の印 */
#define DV_CUR       0x46bd          /* 読んでいるバイト */
#define DV_COL       0x4672
#define DV_ROW       0x4673
#define DV_WIDTH     0x4675
#define DV_PUTVEC    0xb265          /* 1 字出す先（0x9e53 = 窓、折り返しあり） */
#define PUT_WINDOW   0x9e53

/* 圧縮文字列の読み取り（0x7b8f と同じ）。状態は DOS のメモリでなくここで持つ */
typedef struct { uint16_t ptr; uint8_t bits, flags, cur; } bstate;
static uint8_t read5(bstate *st, uint32_t ds)
{
    int k;
    uint8_t al = 0;
    for (k = 0; k < 5; k++) {
        uint8_t cf;
        if (st->bits & 0x80) { st->bits = 7; st->cur = rb(ds + st->ptr); st->ptr++; }
        cf = st->cur & 1; st->cur >>= 1;
        al = (uint8_t)((al >> 1) | (cf << 7));
        st->bits--;
    }
    return (uint8_t)(al >> 3);
}
static uint8_t bgetc(bstate *st, uint32_t ds, uint16_t table)
{
    uint8_t code, ch;
    st->flags >>= 1;                              /* clc; rcr */
    for (;;) {
        code = read5(st, ds);
        if (code != 0x1e) break;
        st->flags = (uint8_t)((st->flags >> 1) | 0x80);   /* stc; rcr */
    }
    if (code == 0x1f) code = (uint8_t)(read5(st, ds) + 0x1e);
    ch = rb(ds + (uint16_t)(table + code));
    st->flags >>= 1;
    if ((st->flags & 0x40) && ch >= 0x61 && ch < 0x7b) ch -= 0x20;
    return ch;
}

/* 出している最中の文字列（入れ子になる: 名前を差し込む 0x0b など。SP で見分ける） */
#define MAXSTR 1024
typedef struct {
    int kind;                 /* 1 平文, 2 圧縮 */
    uint16_t sp;              /* コールバックの時の SP */
    uint16_t start;           /* 平文: 始まりの位置 */
    bstate b0, b1;            /* 圧縮: 始まりと終わりの状態 */
    uint8_t src[MAXSTR];      /* 原文 */
    int srclen;
    const uint16_t *tr;       /* 訳（NULL なら原文のまま） */
    int trlen, pos;
    uint16_t cur;             /* 平文: 今出している字の位置として [4680h] に入れた値 */
    int emitted, nlpos;
} strst;
static strst g_st[8];
static int g_nst;

static strst *get_state(int kind)
{
    uint16_t sp = R_SP;
    strst *s;
    uint32_t ds = PHYS_DS;
    while (g_nst > 0 && g_st[g_nst - 1].sp < sp) g_nst--;     /* 途中で捨てられたもの */
    if (g_nst > 0) {
        s = &g_st[g_nst - 1];
        if (s->sp == sp && s->kind == kind) {
            if (kind == 1 && s->emitted && (s->cur == rw(ds + DV_PTR) || (uint16_t)(s->cur + 1) == rw(ds + DV_PTR))) return s;
            if (kind == 2 && s->b0.ptr == rw(ds + DV_PTR) && s->b0.bits == rb(ds + DV_BITS) && s->b0.cur == rb(ds + DV_CUR)) return s;
        }
    }
    if (g_nst == 8) g_nst = 7;
    s = &g_st[g_nst++];
    memset(s, 0, sizeof *s);
    s->nlpos = -1;
    s->kind = kind; s->sp = sp; s->srclen = 0; s->pos = 0; s->tr = NULL;
    if (kind == 1) {
        uint16_t p = rw(ds + DV_PTR);
        s->start = p;
        while (s->srclen < MAXSTR - 1) {
            uint8_t c = rb(ds + (uint16_t)(p + s->srclen));
            if (!c) break;
            s->src[s->srclen++] = c;
        }
    } else {
        bstate st;
        uint16_t table = rw(ds + DV_TABLE);
        st.ptr = rw(ds + DV_PTR); st.bits = rb(ds + DV_BITS); st.flags = rb(ds + DV_FLAGS); st.cur = rb(ds + DV_CUR);
        s->b0 = st;
        while (s->srclen < MAXSTR - 1) {
            uint8_t c = bgetc(&st, ds, table);
            if (!c) break;
            s->src[s->srclen++] = c;
        }
        s->b1 = st;
    }
    s->tr = find_tr(s->src, s->srclen, &s->trlen);
    if (g_logon) {
        int i;
        dbg("%s%c %08x \"", s->tr ? "T" : "-", kind == 1 ? 'P' : 'C', fnv(s->src, s->srclen));
        for (i = 0; i < s->srclen; i++) {
            uint8_t c = s->src[i];
            if (c >= 0x20 && c < 0x7f && c != '\\' && c != '"') dbg("%c", c); else dbg("\\x%02x", c);
        }
        dbg("\"\n");
    }
    return s;
}
/* 次に出す 1 字（0 で終わり）。字形は枠に入れて 0xA0-0xFF にする */
static int g_pending_glyph = -1;
static uint8_t next_char(strst *s)
{
    uint32_t ds = PHYS_DS;
    if (!s->tr) return s->pos < s->srclen ? s->src[s->pos++] : 0;
    if (s->pos >= s->trlen) return 0;
    {
        uint16_t u = s->tr[s->pos];
        if (u < 0x100) { s->pos++; return (uint8_t)u; }
        /* 窓に出している時は、行が一杯なら先に改行する（ゲームの折り返しは空白でしか切れない） */
        if (rw(ds + DV_PUTVEC) == PUT_WINDOW && !rb(ds + 0x46f0) && s->nlpos != s->pos) {
            int col = rb(ds + DV_COL), w = rb(ds + DV_WIDTH);
            int g = u - 0x100;
            int nextk = (s->pos + 1 < s->trlen && s->tr[s->pos + 1] >= 0x100 && g_kinsoku) ? g_kinsoku[s->tr[s->pos + 1] - 0x100] : 0;
            if (col > w || (col == w && (nextk & 1)) || (col == w - 1 && (nextk & 1) && 0)) {
                (void)g;
                s->nlpos = s->pos;
                return 0x0d;      /* 改行（pos は進めない） */
            }
        }
        s->pos++;
        return slot_alloc((uint16_t)(u - 0x100));
    }
}
/* 1 字出す（putc を呼んで、終わったらこのコールバックに戻る） */
static void emit(uint8_t c, uint16_t back)
{
    SET_AL(c);
    push16(back);
    SET16(8, WL_PUTC);
}

/* CB 0: 平文の文字列（0x787a: xor bx,bx; mov di,[4680h]; mov al,[bx+di] ... の代わり）
 *   [4680h] は今の字の位置にしておく（0x09 は次のバイトを引数として読み、[4680h] を 1 進める） */
static uint32_t __cdecl cb_plain(void)
{
    strst *s = get_state(1);
    uint32_t ds = PHYS_DS;
    uint8_t c;
    if (s->emitted && rw(ds + DV_PTR) == (uint16_t)(s->cur + 1)) s->pos++;    /* 引数を読まれた */
    if (!s->tr) {
        s->cur = (uint16_t)(s->start + s->pos);
        c = s->pos < s->srclen ? s->src[s->pos++] : 0;
    } else {
        int at = s->pos;
        c = next_char(s);
        s->cur = s->start;
        if (c && c < 0x20 && at < s->trlen && s->tr[at] < 0x20) {
            /* 制御の字: 原文の同じ番目の同じ字の位置（引数がそこから読まれる） */
            int k, nth = 0, i;
            for (k = 0; k < at; k++) if (s->tr[k] == s->tr[at]) nth++;
            for (i = 0; i < s->srclen; i++) if (s->src[i] == c && nth-- == 0) { s->cur = (uint16_t)(s->start + i); break; }
        }
    }
    if (c) {
        ww(ds + DV_PTR, s->cur);
        s->emitted = 1;
        emit(c, WL_PLAIN_CB);
        return 0;
    }
    ww(ds + DV_PTR, (uint16_t)(s->start + s->srclen));   /* 0 の位置（この後 788fh で 1 進む） */
    g_nst--;
    SET16(3, 0); SET16(7, s->start + s->srclen);
    return 0;
}
/* CB 1: 圧縮文字列（0x78a9: call 7b8fh; cmp al,0; je; call putc; jmp の代わり） */
static uint32_t __cdecl cb_cstr(void)
{
    strst *s = get_state(2);
    uint8_t c = next_char(s);
    uint32_t ds = PHYS_DS;
    if (c) { emit(c, WL_CSTR_CB); return 0; }
    ww(ds + DV_PTR, s->b1.ptr); wb(ds + DV_BITS, s->b1.bits); wb(ds + DV_FLAGS, s->b1.flags); wb(ds + DV_CUR, s->b1.cur);
    g_nst--;
    SET_AL(0);
    return 0;
}
/* CB 2: ドライバの 1 字描く所（WLA.BIN 0x651: and ax,7fh; mov si,ax の代わり）
 *   al = 字 - 20h。0x80 以上は枠の字: 字形を字体の空き（7ch 番 = 9ch の字、空白）に書いてそれを描かせる */
#define FONT_OFF 0xca60
#define SCRATCH  0x7c
static uint32_t __cdecl cb_draw(void)
{
    uint8_t a = (uint8_t)R_AX;
    if (a & 0x80) {
        int s = a - 0x80;
        uint32_t fseg = (uint32_t)rw(PHYS_DS + 0x9165) << 4;
        uint32_t dst = fseg + FONT_OFF + SCRATCH * 8;
        int k;
        if (s < NSLOT && g_slot[s] < g_nglyph) {
            const uint8_t *g = g_glyph + g_slot[s] * 8;
            for (k = 0; k < 8; k++) wb(dst + k, g[k]);
        } else for (k = 0; k < 8; k++) wb(dst + k, 0);
        SET16(0, SCRATCH);
    } else SET16(0, a & 0x7f);
    SET16(6, R_AX);
    return 0;
}
/* 下の窓の履歴（1ae2h:8ce0h、40 x 256 行）の影: 枠の字はどの字形かを覚えておく */
static uint16_t g_hist[0x2800];
/* CB 3: 履歴に書く所（0x9f66: mov es:[bx+si],al; inc si） */
static uint32_t __cdecl cb_hstore(void)
{
    uint16_t a = (uint16_t)(R_BX + R_SI);
    uint8_t v = (uint8_t)R_AX;
    wb(PHYS_ES + a, v);
    if (a >= 0x8ce0 && a < 0x8ce0 + 0x2800)
        g_hist[a - 0x8ce0] = (v & 0x80) && (v - 0x80) < NSLOT ? (uint16_t)(g_slot[v - 0x80] + 1) : 0;
    SET16(6, R_SI + 1);
    return 0;
}
/* CB 4: 履歴を描き直す所（0x79fa: mov al,es:[bx+si]; call 0fh） */
static uint32_t __cdecl cb_hload(void)
{
    uint16_t a = (uint16_t)(R_BX + R_SI);
    uint8_t v = rb(PHYS_ES + a);
    if ((v & 0x80) && a >= 0x8ce0 && a < 0x8ce0 + 0x2800 && g_hist[a - 0x8ce0])
        v = (uint8_t)(slot_alloc((uint16_t)(g_hist[a - 0x8ce0] - 1)) - 0x20);
    SET_AL(v);
    push16(WL_HLOAD_RET);
    SET16(8, 0x000f);
    return 0;
}

/* ------------------------------------------------------------ コールバックの登録 */
#define NCB 5
static int g_cb[NCB];
static int g_cb_ok;
static int install_callbacks(void)
{
    static void *fn[NCB] = {(void*)cb_plain, (void*)cb_cstr, (void*)cb_draw, (void*)cb_hstore, (void*)cb_hload};
    uint32_t *h = (uint32_t*)A_CB_HANDLERS;
    int i, n = 0;
    if (g_cb_ok) return 1;
    if (!dbx_ok()) return 0;
    for (i = 127; i > 16 && n < NCB; i--)
        if (h[i] == A_CB_ILLEGAL) g_cb[n++] = i;
    if (n < NCB) { dbg("no free callbacks\n"); return 0; }
    for (i = 0; i < NCB; i++) h[g_cb[i]] = (uint32_t)(uintptr_t)fn[i];
    g_cb_ok = 1;
    dbg("callbacks %d %d %d %d %d\n", g_cb[0], g_cb[1], g_cb[2], g_cb[3], g_cb[4]);
    return 1;
}

/* ------------------------------------------------------------ EXEPACK を外す */
static uint8_t *unexepack(const uint8_t *d, size_t n, size_t *outn)
{
    uint16_t hpar, cs, minal, maxal, rip, rcs, rsp, rss, dlen, xsize, nrel = 0;
    const uint8_t *img, *stub, *p;
    uint32_t eh, isz, src, dst, total, hp, orig_pars, new_pars, i, seg, k;
    uint8_t *buf, *out;
    uint16_t rel[2 * 256];
    if (n < 0x40 || d[0] != 'M' || d[1] != 'Z') return NULL;
    hpar = *(const uint16_t*)(d + 8); minal = *(const uint16_t*)(d + 10); maxal = *(const uint16_t*)(d + 12);
    cs = *(const uint16_t*)(d + 0x16);
    img = d + hpar * 16; isz = (uint32_t)(n - hpar * 16);
    eh = cs * 16u;
    if (eh + 18 > isz) return NULL;
    rip = *(const uint16_t*)(img + eh); rcs = *(const uint16_t*)(img + eh + 2);
    xsize = *(const uint16_t*)(img + eh + 6); rsp = *(const uint16_t*)(img + eh + 8); rss = *(const uint16_t*)(img + eh + 10);
    dlen = *(const uint16_t*)(img + eh + 12);
    if (memcmp(img + eh + 14, "RB", 2) && memcmp(img + eh + 16, "RB", 2)) return NULL;
    total = dlen * 16u > eh ? dlen * 16u : eh;
    buf = (uint8_t*)calloc(total, 1);
    memcpy(buf, img, eh);
    src = eh - 1;
    while (img[src] == 0xff) src--;
    dst = total - 1;
    for (;;) {
        uint8_t c = img[src--];
        uint16_t cnt = (uint16_t)((img[src] << 8) | img[src - 1]);
        src -= 2;
        if ((c & 0xfe) == 0xb0) { uint8_t b = img[src--]; while (cnt--) buf[dst--] = b; }
        else if ((c & 0xfe) == 0xb2) { while (cnt--) buf[dst--] = img[src--]; }
        else { free(buf); return NULL; }
        if (c & 1) break;
    }
    stub = img + eh;
    p = NULL;
    for (i = 0; i + 22 < xsize; i++) if (!memcmp(stub + i, "Packed file is corrupt", 22)) { p = stub + i + 22; break; }
    if (!p) { free(buf); return NULL; }
    for (seg = 0; seg < 16; seg++) {
        uint16_t c = *(const uint16_t*)p; p += 2;
        for (k = 0; k < c && nrel < 256; k++) { rel[2 * nrel] = *(const uint16_t*)p; rel[2 * nrel + 1] = (uint16_t)(seg * 0x1000); nrel++; p += 2; }
    }
    hp = (0x1c + 4u * nrel + 15) / 16;
    orig_pars = (isz + 15) / 16 + minal;
    new_pars = (total + 15) / 16;
    *outn = hp * 16 + total;
    out = (uint8_t*)calloc(*outn, 1);
    {
        uint16_t h[14] = {0x5a4d, (uint16_t)(*outn % 512), (uint16_t)((*outn + 511) / 512), nrel, (uint16_t)hp,
                          (uint16_t)(orig_pars > new_pars ? orig_pars - new_pars : 0), maxal, rss, rsp, 0, rip, rcs, 0x1c, 0};
        memcpy(out, h, 28);
    }
    memcpy(out + 0x1c, rel, 4u * nrel);
    memcpy(out + hp * 16, buf, total);
    free(buf);
    return out;
}

/* ------------------------------------------------------------ 差し替えるファイルを作る */
static int put_cb(uint8_t *p, int k) { p[0] = 0xfe; p[1] = 0x38; p[2] = (uint8_t)g_cb[k]; p[3] = 0; return 4; }
static int build_wl(const char *src, const char *dst)
{
    FILE *f = fopen(src, "rb");
    uint8_t *d, *u, *img;
    size_t n, un = 0;
    int ok = 0;
    static const uint8_t c_plain[] = {0x33, 0xdb, 0x8b, 0x3e, 0x80, 0x46, 0x8a, 0x01, 0x84, 0xc0, 0x74, 0x09, 0xe8, 0x13, 0x00};
    static const uint8_t c_cstr[] = {0xe8, 0xe3, 0x02, 0x3c, 0x00, 0x74, 0x05, 0xe8, 0xe9, 0xff, 0xeb, 0xf4, 0xe8, 0x74, 0x14, 0xc3};
    static const uint8_t c_hst[] = {0x26, 0x88, 0x00, 0x46, 0x8b, 0xd6, 0xeb, 0xee};
    static const uint8_t c_hld[] = {0x26, 0x8a, 0x00, 0xe8, 0x0f, 0x86, 0x5b};
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    d = (uint8_t*)malloc(n);
    fread(d, 1, n, f); fclose(f);
    u = unexepack(d, n, &un);
    free(d);
    if (!u) { dbg("unexepack failed\n"); return 0; }
    img = u + *(uint16_t*)(u + 8) * 16;
    if (memcmp(img + 0x787a, c_plain, sizeof c_plain) || memcmp(img + 0x78a9, c_cstr, sizeof c_cstr) ||
        memcmp(img + 0x9f66, c_hst, sizeof c_hst) || memcmp(img + 0x79fa, c_hld, sizeof c_hld)) {
        dbg("WL.EXE: unknown version\n");
        free(u);
        return 0;
    }
    /* 平文: cb; jmp 788fh */
    put_cb(img + 0x787a, 0); img[0x787e] = 0xeb; img[0x787f] = 0x0f;
    /* 圧縮: cb; call 8d2ch; ret */
    put_cb(img + 0x78a9, 1);
    img[0x78ad] = 0xe8; img[0x78ae] = 0x7c; img[0x78af] = 0x14; img[0x78b0] = 0xc3;
    memset(img + 0x78b1, 0x90, 8);
    /* 履歴 */
    put_cb(img + 0x9f66, 3);
    put_cb(img + 0x79fa, 4); img[0x79fe] = 0x90; img[0x79ff] = 0x90;
    f = fopen(dst, "wb");
    if (f) { ok = fwrite(u, 1, un, f) == un; fclose(f); }
    free(u);
    return ok;
}
static int build_wla(const char *src, const char *dst)
{
    FILE *f = fopen(src, "rb");
    uint8_t d[0x2000];
    size_t n;
    static const uint8_t c_draw[] = {0x25, 0x7f, 0x00, 0x8b, 0xf0, 0xd1, 0xe6};
    if (!f) return 0;
    n = fread(d, 1, sizeof d, f); fclose(f);
    if (n < 0x700 || memcmp(d + 0x651, c_draw, sizeof c_draw)) { dbg("WLA.BIN: unknown version\n"); return 0; }
    put_cb(d + 0x651, 2); d[0x655] = 0x90;
    f = fopen(dst, "wb");
    if (!f) return 0;
    fwrite(d, 1, n, f); fclose(f);
    return 1;
}

/* ------------------------------------------------------------ fopen */
typedef FILE *(__cdecl *fopen_t)(const char *, const char *);
static fopen_t real_fopen;
static int g_enabled;
static int build_para(char *out);
static int ends_with(const char *s, const char *t)
{
    size_t a = strlen(s), b = strlen(t);
    return a >= b && !_stricmp(s + a - b, t);
}
static FILE *__cdecl hook_fopen(const char *path, const char *mode)
{
    char p[MAX_PATH];
    if (g_logon && path && !strstr(path, "wljp")) dbg("fopen %s %s\n", path, mode);
    if (!g_enabled || !path || !mode || mode[0] != 'r') return real_fopen(path, mode);
    if (ends_with(path, "paragraphs.bin")) {
        if (build_para(p)) { dbg("-> %s\n", p); return real_fopen(p, mode); }
    } else if (ends_with(path, "rom\\WL.exe") || ends_with(path, "rom/WL.exe") || ends_with(path, "rom\\WL.EXE")) {
        if (install_callbacks()) {
            wsprintfA(p, "%s\\WL.EXE", g_cache);
            if (build_wl(path, p)) { dbg("-> %s\n", p); return real_fopen(p, mode); }
        }
    } else if (ends_with(path, "WLA.BIN") && (strstr(path, "rom\\data") || strstr(path, "rom/data"))) {
        if (install_callbacks()) {
            wsprintfA(p, "%s\\WLA.BIN", g_cache);
            if (build_wla(path, p)) { dbg("-> %s\n", p); return real_fopen(p, mode); }
        }
    }
    return real_fopen(path, mode);
}

/* ------------------------------------------------------------ パラグラフ画像（paragraphs.bin を日本語版に） */
/* LZ4 ブロックを展開する（壊れていたら -1） */
static int lz4_dec(const uint8_t *s, size_t sn, uint8_t *d, size_t dn)
{
    const uint8_t *se = s + sn;
    uint8_t *d0 = d, *de = d + dn;
    while (s < se) {
        unsigned tok = *s++;
        size_t lit = tok >> 4, ml;
        if (lit == 15) { unsigned b; do { if (s >= se) return -1; b = *s++; lit += b; } while (b == 255); }
        if ((size_t)(se - s) < lit || (size_t)(de - d) < lit) return -1;
        memcpy(d, s, lit); d += lit; s += lit;
        if (s >= se) break;
        if (se - s < 2) return -1;
        {
            size_t off = s[0] | (s[1] << 8);
            s += 2;
            ml = tok & 15;
            if (ml == 15) { unsigned b; do { if (s >= se) return -1; b = *s++; ml += b; } while (b == 255); }
            ml += 4;
            if (off == 0 || (size_t)(d - d0) < off || (size_t)(de - d) < ml) return -1;
            { const uint8_t *m = d - off; while (ml--) *d++ = *m++; }
        }
    }
    return (int)(d - d0);
}
/* 展開して %LOCALAPPDATA%\WL_JP\paragraphs_<大きさ>.bin に置く（同じ物があれば使う） */
static int build_para(char *out)
{
    uint32_t n;
    uint8_t *buf;
    FILE *f;
    if (!g_para || g_para_len < 8) return 0;
    n = *(const uint32_t*)g_para;
    wsprintfA(out, "%s\\paragraphs_%08x_%08x.bin", g_cache, n, fnv(g_para + 4, g_para_len > 4096 ? 4096 : g_para_len - 4));
    f = fopen(out, "rb");
    if (f) {
        long sz;
        fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f);
        if ((uint32_t)sz == n) return 1;
    }
    buf = (uint8_t*)malloc(n);
    if (!buf) return 0;
    if (lz4_dec(g_para + 4, g_para_len - 4, buf, n) != (int)n) { dbg("para lz4 error\n"); free(buf); return 0; }
    f = fopen(out, "wb");
    if (!f) { free(buf); return 0; }
    fwrite(buf, 1, n, f); fclose(f); free(buf);
    dbg("para built %s\n", out);
    return 1;
}

/* ------------------------------------------------------------ 設定ファイル（msvcp100 の ifstream → _fsopen）
 *   拡大フィルタの hq3x などは 1 ドットの線をにじませて漢字が読めなくなるので、読む時だけ normal に替える
 *   （ユーザーの設定ファイルは書き換えない） */
typedef FILE *(__cdecl *fsopen_t)(const char *, const char *, int);
static fsopen_t real_fsopen;
static const char *scaler_sub(const char *v, size_t n)
{
    static const char *tab[][2] = {{"hq2x", "normal2x"}, {"hq3x", "normal3x"}, {"advmame2x", "normal2x"}, {"advmame3x", "normal3x"},
        {"advinterp2x", "normal2x"}, {"advinterp3x", "normal3x"}, {"2xsai", "normal2x"}, {"super2xsai", "normal2x"}, {"supereagle", "normal2x"}};
    int i;
    for (i = 0; i < (int)(sizeof(tab) / sizeof(tab[0])); i++)
        if (strlen(tab[i][0]) == n && !_strnicmp(v, tab[i][0], n)) return tab[i][1];
    return NULL;
}
static FILE *__cdecl hook_fsopen(const char *path, const char *mode, int sh)
{
    if (g_logon) dbg("fsopen %s %s %d\n", path ? path : "-", mode ? mode : "-", sh);
    if (g_enabled && path && mode && mode[0] == 'r' && ends_with(path, ".conf")) {
        FILE *f = fopen(path, "rb");   /* 自分の CRT で読む（msvcr100 の FILE は使えない） */
        if (f) {
            char *buf, *out, *p, *o, np[MAX_PATH];
            long n;
            int changed = 0;
            fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
            buf = (char*)malloc(n + 1); out = (char*)malloc(n + 64);
            n = (long)fread(buf, 1, n, f); buf[n] = 0; fclose(f);
            for (p = buf, o = out; *p; ) {
                char *e = strchr(p, '\n');
                size_t ln = e ? (size_t)(e - p + 1) : strlen(p);
                const char *q = p;
                while (q < p + ln && (*q == ' ' || *q == '\t')) q++;
                if (!_strnicmp(q, "scaler", 6)) {
                    const char *v = q + 6, *ve, *sub;
                    while (*v == ' ' || *v == '\t') v++;
                    if (*v == '=') {
                        v++;
                        while (*v == ' ' || *v == '\t') v++;
                        for (ve = v; ve < p + ln && *ve > ' '; ve++) ;
                        sub = scaler_sub(v, (size_t)(ve - v));
                        if (sub) {
                            memcpy(o, p, v - p); o += v - p;
                            lstrcpyA(o, sub); o += strlen(sub);
                            memcpy(o, ve, p + ln - ve); o += p + ln - ve;
                            p += ln; changed = 1;
                            continue;
                        }
                    }
                }
                memcpy(o, p, ln); o += ln; p += ln;
            }
            if (changed) {
                const char *b = strrchr(path, '\\'), *b2 = strrchr(path, '/');
                if (b2 > b) b = b2;
                wsprintfA(np, "%s\\%s", g_cache, b ? b + 1 : path);
                f = fopen(np, "wb");
                if (f) {
                    fwrite(out, 1, o - out, f); fclose(f);
                    dbg("conf %s -> %s (scaler)\n", path, np);
                    free(buf); free(out);
                    return real_fsopen(np, mode, sh);
                }
            }
            free(buf); free(out);
        }
    }
    return real_fsopen(path, mode, sh);
}

/* ------------------------------------------------------------ SDL_SetVideoMode（テスト: 中身を確かめるだけ） */
typedef void *(__cdecl *svm_t)(int, int, int, uint32_t);
static svm_t real_svm;
static void *__cdecl hook_svm(int w, int h, int bpp, uint32_t fl)
{
    dbx_ok();
    dbg("SetVideoMode %dx%d %d %x\n", w, h, bpp, fl);
    return real_svm(w, h, bpp, fl);
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p)
{
    (void)p;
    if (r == DLL_PROCESS_ATTACH) {
        char *s, f[MAX_PATH];
        DisableThreadLibraryCalls(h);
        GetModuleFileNameA(NULL, g_dir, MAX_PATH);
        s = strrchr(g_dir, '\\');
        if (s) *s = 0;
        wsprintfA(f, "%s\\wljp_log", g_dir);
        g_logon = GetFileAttributesA(f) != INVALID_FILE_ATTRIBUTES;
        wsprintfA(f, "%s\\wljp_en", g_dir);
        g_enabled = GetFileAttributesA(f) == INVALID_FILE_ATTRIBUTES;   /* テスト: 英語のまま動かす */
        proxy_init();
        testio_init();
        if (!GetEnvironmentVariableA("LOCALAPPDATA", g_cache, MAX_PATH)) lstrcpyA(g_cache, g_dir);
        lstrcatA(g_cache, "\\WL_JP");
        CreateDirectoryA(g_cache, NULL);
        if (!load_blob(h)) { dbg("no blob\n"); g_enabled = 0; }
        patch_iat(GetModuleHandleA(NULL), "MSVCR100.dll", "fopen", (void*)hook_fopen, (void**)&real_fopen);
        patch_iat(GetModuleHandleA(NULL), "SDL.dll", "SDL_SetVideoMode", (void*)hook_svm, (void**)&real_svm);
        {
            HMODULE cp = GetModuleHandleA("msvcp100.dll");
            if (cp) patch_iat(cp, "MSVCR100.dll", "_fsopen", (void*)hook_fsopen, (void**)&real_fsopen);
            if (!real_fsopen) real_fsopen = (fsopen_t)GetProcAddress(GetModuleHandleA("msvcr100.dll"), "_fsopen");
        }
        dbg("attach enabled=%d\n", g_enabled);
    }
    return TRUE;
}
