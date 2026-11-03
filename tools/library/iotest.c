/* Diagnostic build: what the kernel's file reads and writes do on a big file.
 *
 * Takes the place of index.c in the blob (build.sh --library-iotest).  After the 100-file scan
 * it creates scratch files in the card root and runs these tests, each stamping every 512-byte
 * sector it writes with the file, the byte offset, and a pass number, and checking every
 * sector it reads (8 KB per call throughout):
 *
 *   T1  MUSIC.TMP, created with a 4 MB size: sequential writes of all 4 MB (pass 1), then a
 *       sequential read of all of it
 *   T2  MUSIC.TMP: 1,024 random reads
 *   T3  MUSIC.TMP: 256 random writes (pass 2), then a sequential read of all of it
 *   T4  MUSIC.TM2, created with a 4 MB size and never written in order: writes at 1 MB, 2 MB,
 *       3 MB and 0 (pass 1), then each read back, then a write at 64 KB and all four read again
 *   T5  MUSIC.TM3, created with a 4 MB size: the first 64 KB written in order, then 64 random
 *       writes anywhere in the 4 MB (pass 2), then every written block read back
 *   T6  MUSIC.TM4, created with size 0: 1 MB of sequential writes, then read back
 *   T7  MUSIC.LIB: 256 random writes at 1-9 MB (pass 3), then each read back
 *
 * The first run (2026-11-02, on a leftover 95 KB MUSIC.TMP from the probe build) failed every
 * MUSIC.TMP test from the first 32 KB cluster boundary on, while every MUSIC.LIB test passed:
 * the kernel does not extend a file past the size it was created with.  The second run, on
 * freshly created files of the right size, passed T1 to T5 and T7 (sequential, random and
 * out-of-order first writes all read back right with 8 KB sector-aligned calls); T6 failed,
 * since a file created with size 0 cannot be written at all; and one create on an existing
 * name left two directory entries called MUSIC.TMP.  The results are written as text into sector 1 of MUSIC.LIB (song records 0 and 1): per test
 * the number of bad sectors, and for the first bad one the offset asked for and the stamp found
 * there (pass/file/offset).  The scratch files are left on the card.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define RAMB     0x9fc00000u
#define MEM      0xbfc00000u                                         /* uncached alias */
#define FS_OP    ((u32 (*)(u32, u32, u32, u32, u32))0xbfc1640du)
#define PUMP     ((u32 (*)(void))0xbfc165e5u)
#define PROGRESS ((u32 (*)(u32))0x401d4cb1u)
#define V32(o) (*(volatile u32 *)(RAMB + 0x15400u + (o)))
#define V16(o) (*(volatile u16 *)(RAMB + 0x15400u + (o)))

#define WBUF     ((u8 *)(MEM + 0x22000u))       /* 8 KB write buffer */
#define RBUF     ((u8 *)(MEM + 0x24000u))       /* 8 KB read buffer */
#define TXT      ((u8 *)(MEM + 0x26000u))       /* the result text, one sector */
#define BLK      8192u
#define TMP_LEN  0x400000u                       /* 512 blocks */
#define LIB_BASE 0x100000u                       /* MUSIC.LIB test area: 1024 blocks from 1 MB */
#define MAGIC    0x494f0000u
#define NT       7

typedef struct { u32 pass, fid, off; } stamp_t;
typedef struct {
    u32 ctx, hL, h, seed, fid;
    u32 created[5], opened[5];                   /* per scratch file: the create and open results */
    u32 bad[NT], at[NT]; stamp_t got[NT];        /* per test: bad sectors, first bad offset, stamp found */
    u8 pt[1024];                                 /* expected pass per 8 KB block of the current file */
} st_t;
#define S (*(volatile st_t *)(RAMB + 0x3a000u))

static void seek(u32 h, u32 pos);
static u32 rd(u32 h, void *dst);
static u32 wr(u32 h, const void *src);
static u32 rnd(void);
static void stamp(u32 fid, u32 off, u32 pass);
static void check(u32 t, u32 fid, u32 off);
static void wblk(u32 t, u32 blk, u32 pass);
static void rblk(u32 t, u32 blk);
static void rall(u32 t, u32 nblk);
static u32 newfile(u32 i, const char *nm, u32 size);
static void name(const char *s);
static void result(void);

void index_passes(void)
{
    u32 i, p, h;
    S.ctx = V32(0x10);
    S.hL = V32(0x0c);
    S.seed = 12345;
    for (i = 0; i < NT; i++) { S.bad[i] = 0; S.at[i] = 0xffffffffu; }
    for (i = 0; i < 5; i++) S.created[i] = S.opened[i] = 0;

    /* T1-T3: MUSIC.TMP */
    for (i = 0; i < 1024; i++) S.pt[i] = 0;
    h = newfile(0, "MUSIC.TMP", TMP_LEN);
    if (h) {
        seek(h, 0);
        for (i = 0; i < 512; i++) { stamp(2, i * BLK, 1); wr(h, WBUF); S.pt[i] = 1; if ((i & 31) == 0) PUMP(); }
        rall(0, 512);
        PROGRESS(0xff);
        for (i = 0; i < 1024; i++) { rblk(1, rnd() % 512); if ((i & 31) == 0) PUMP(); }
        PROGRESS(0xff);
        for (i = 0; i < 256; i++) { wblk(2, rnd() % 512, 2); if ((i & 31) == 0) PUMP(); }
        rall(2, 512);
        PROGRESS(0xff);
        FS_OP(S.ctx, h, 0, 0, 7);
    }
    /* T4: MUSIC.TM2, written out of order from the start */
    for (i = 0; i < 1024; i++) S.pt[i] = 0;
    h = newfile(1, "MUSIC.TM2", TMP_LEN);
    if (h) {
        wblk(3, 128, 1); wblk(3, 256, 1); wblk(3, 384, 1); wblk(3, 0, 1);
        rblk(3, 128); rblk(3, 256); rblk(3, 384); rblk(3, 0);
        wblk(3, 8, 1);
        rblk(3, 128); rblk(3, 256); rblk(3, 384); rblk(3, 0); rblk(3, 8);
        FS_OP(S.ctx, h, 0, 0, 7);
    }
    PROGRESS(0xff);
    /* T5: MUSIC.TM3, the first 64 KB in order, then anywhere */
    for (i = 0; i < 1024; i++) S.pt[i] = 0;
    h = newfile(2, "MUSIC.TM3", TMP_LEN);
    if (h) {
        seek(h, 0);
        for (i = 0; i < 8; i++) { stamp(2, i * BLK, 1); wr(h, WBUF); S.pt[i] = 1; }
        for (i = 0; i < 64; i++) wblk(4, rnd() % 512, 2);
        for (p = 0; p < 512; p++) if (S.pt[p]) rblk(4, p);
        FS_OP(S.ctx, h, 0, 0, 7);
    }
    PROGRESS(0xff);
    /* T6: MUSIC.TM4, created empty and extended in order */
    for (i = 0; i < 1024; i++) S.pt[i] = 0;
    h = newfile(3, "MUSIC.TM4", 0);
    if (h) {
        seek(h, 0);
        for (i = 0; i < 128; i++) { stamp(2, i * BLK, 1); wr(h, WBUF); S.pt[i] = 1; if ((i & 31) == 0) PUMP(); }
        rall(5, 128);
        FS_OP(S.ctx, h, 0, 0, 7);
    }
    PROGRESS(0xff);
    /* T7: MUSIC.LIB at 1-9 MB */
    for (i = 0; i < 1024; i++) S.pt[i] = 0;
    S.h = S.hL; S.fid = 1;
    for (i = 0; i < 256; i++) { wblk(6, rnd() % 1024, 3); if ((i & 31) == 0) PUMP(); }
    for (p = 0; p < 1024; p++) if (S.pt[p]) rblk(6, p);
    PROGRESS(0xff);
    result();
    for (i = 0x40; i < 0x9c; i += 2) V16(i) = 0;  /* an empty library */
}

static u32 newfile(u32 i, const char *nm, u32 size)   /* create in the root with a size, as the stock does; open */
{
    u32 h;
    FS_OP(S.ctx, 0x3a, 0, 0, 0);
    name(nm);
    h = FS_OP(S.ctx, RAMB + 0x15538u, size, 0, 6);
    S.created[i] = h;
    if (h) { FS_OP(S.ctx, 2, h, 0, 0x13); FS_OP(S.ctx, h, 0, 0, 7); h = FS_OP(S.ctx, RAMB + 0x15538u, 0, 0, 5); }
    S.opened[i] = h;
    S.h = h; S.fid = 2;
    return h;
}

static void seek(u32 h, u32 pos) { FS_OP(S.ctx, pos, 0, h, 0xb); }
static u32 rd(u32 h, void *dst) { return FS_OP(S.ctx, (u32)dst, BLK, h, 0xd); }
static u32 wr(u32 h, const void *src) { return FS_OP(S.ctx, (u32)src, BLK, h, 0xe); }
static u32 rnd(void) { S.seed = S.seed * 1103515245u + 12345u; return S.seed >> 8; }
static u32 base(void) { return S.fid == 1 ? LIB_BASE : 0; }

static void wblk(u32 t, u32 blk, u32 pass)       /* seek and write one stamped block of the current file */
{
    (void)t;
    seek(S.h, base() + blk * BLK); stamp(S.fid, base() + blk * BLK, pass); wr(S.h, WBUF); S.pt[blk] = pass;
}
static void rblk(u32 t, u32 blk)                 /* seek, read and check one block */
{
    seek(S.h, base() + blk * BLK); rd(S.h, RBUF); check(t, S.fid, base() + blk * BLK);
}
static void rall(u32 t, u32 nblk)                /* read the first nblk blocks in order */
{
    u32 i;
    seek(S.h, base());
    for (i = 0; i < nblk; i++) { rd(S.h, RBUF); check(t, S.fid, base() + i * BLK); if ((i & 31) == 0) PUMP(); }
}

static void stamp(u32 fid, u32 off, u32 pass)    /* 16 stamped sectors in WBUF */
{
    u32 s, k;
    for (s = 0; s < 16; s++) {
        u32 *w = (u32 *)(WBUF + 512 * s), o = off + 512 * s;
        w[0] = MAGIC | pass; w[1] = fid; w[2] = o; w[3] = ~o;
        for (k = 4; k < 128; k++) w[k] = o ^ (k * 0x01010101u);
    }
}

static void check(u32 t, u32 fid, u32 off)       /* RBUF against the stamps expected at off */
{
    u32 s, k;
    for (s = 0; s < 16; s++) {
        const u32 *w = (const u32 *)(RBUF + 512 * s);
        u32 o = off + 512 * s, bad = 0;
        u32 pass = S.pt[(o - base()) / BLK];
        if (w[0] != (MAGIC | pass) || w[1] != fid || w[2] != o || w[3] != ~o) bad = 1;
        for (k = 4; k < 128 && !bad; k++) if (w[k] != (o ^ (k * 0x01010101u))) bad = 1;
        if (bad) {
            if (S.at[t] == 0xffffffffu) { S.at[t] = o; S.got[t].pass = w[0]; S.got[t].fid = w[1]; S.got[t].off = w[2]; }
            S.bad[t]++;
        }
    }
}

static void name(const char *s)                  /* BOM + UTF-16LE in the app's name buffer */
{
    u8 *p = (u8 *)(RAMB + 0x15538u);
    *p++ = 0xff; *p++ = 0xfe;
    while (*s) { *p++ = *s++; *p++ = 0; }
    *p++ = 0; *p = 0;
}

static u8 *put(u8 *p, const char *s) { while (*s) *p++ = *s++; return p; }
static u8 *hex(u8 *p, u32 v)
{
    const char *h = "0123456789abcdef";
    int i;
    for (i = 28; i >= 0; i -= 4) *p++ = h[(v >> i) & 15];
    return p;
}

static void result(void)
{
    u8 *p = TXT;
    u32 i;
    for (i = 0; i < 512; i++) TXT[i] = 0;
    p = put(p, "IOTEST2 files");
    for (i = 0; i < 4; i++) { *p++ = ' '; p = hex(p, S.created[i]); *p++ = '/'; p = hex(p, S.opened[i]); }
    for (i = 0; i < NT; i++) {
        p = put(p, " T"); *p++ = '1' + i; p = put(p, " bad=");
        p = hex(p, S.bad[i]);
        if (S.at[i] != 0xffffffffu) {
            p = put(p, " at="); p = hex(p, S.at[i]);
            p = put(p, " got="); p = hex(p, S.got[i].pass); *p++ = '/'; p = hex(p, S.got[i].fid); *p++ = '/'; p = hex(p, S.got[i].off);
        }
    }
    *p++ = '\n';
    seek(S.hL, 0x200);
    FS_OP(S.ctx, (u32)TXT, 512, S.hL, 0xe);
}
