/* Diagnostic build of the library passes: time the file operations on the player.
 *
 * Takes the place of index.c in the blob (build.sh --library-bench).  Runs seven timed tests
 * on the preallocated MUSIC.LIB, each ending with one notch of the progress bar, so the time
 * between notches is the cost of that test.  The results are written into the first three song
 * records of the 100-file scan, readable from the card (the library is left empty, so the player shows no songs until the real
 * firmware is installed again).
 *
 *   20% -> 30%  T1  16,384 sequential reads of 512 bytes (8 MB)
 *   30% -> 40%  T2   2,048 random seeks in the first 14 MB, each with one 512-byte read
 *   40% -> 50%  T3   2,048 forward seeks, 4 KB apart, each with one 512-byte read
 *   50% -> 60%  T4   1,024 sequential reads of 8 KB (8 MB), checked against 512-byte reads
 *   60% -> 70%  T5  16,384 sequential writes of 512 bytes from 1 MB (8 MB)
 *   70% -> 80%  T6   1,024 sequential writes of 8 KB (8 MB), read back and checked
 *   80% -> 90%  T7  a second file MUSIC.TMP of 4 MB: 2,048 random seeks with one 512-byte
 *                   read each, then writes through one handle read back through the other
 *
 * Song 0 title: "BENCH R8 ok|BAD W8 ok|BAD T ok|BAD|none D ok|BAD r<8 KB read result> w<8 KB
 * write result> t<TMP handle>" (D: MUSIC.TMP was gone after the delete calls).  Songs 1 and 2:
 * the bytes around the pointer the directory walker returns for the first two entries
 * (title: 32 bytes before it, artist: "p" and the pointer, album: 24 bytes from it, as hex),
 * to see what the kernel keeps of a directory entry.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifdef TEST
#define RAMB     0x10000000u
#define MEM      0x10000000u
#define FS_OP    ((u32 (*)(u32, u32, u32, u32, u32))0x00700400u)
#define PUMP     ((u32 (*)(void))0x00700c00u)
#define PROGRESS ((u32 (*)(u32))0x00700c00u)
#else
#define RAMB     0x9fc00000u
#define MEM      0xbfc00000u                                         /* uncached: file data bypasses the cache */
#define FS_OP    ((u32 (*)(u32, u32, u32, u32, u32))0xbfc1640du)
#define PUMP     ((u32 (*)(void))0xbfc165e5u)
#define PROGRESS ((u32 (*)(u32))0x401d4cb1u)
#define WALKER   ((u32 (*)(u32 *))0xbfc1687du)                       /* next matching file */
#endif
#define V16(o) (*(volatile u16 *)(RAMB + 0x15400u + (o)))
#define V32(o) (*(volatile u32 *)(RAMB + 0x15400u + (o)))

#define BBUF     ((u8 *)(MEM + 0x22000u))       /* 64 KB, uncached, free until the real passes start */
#define FILE_LEN 0xe00000u                       /* the tests stay in the first 14 MB */
#define WR_BASE  0x100000u                       /* writes start at 1 MB, past the records */
#define TMP_LEN  0x400000u
typedef struct { u32 ctx, h, seed, r8, w8, ok_r8, ok_w8, ok_tmp, h2, ok_del; u32 ent[2]; } bench_t;
#define B (*(volatile bench_t *)(RAMB + 0x3a680u))

static void b_seek(u32 h, u32 pos);
static u32 b_read(u32 h, void *dst, u32 n);
static u32 b_write(u32 h, const void *src, u32 n);
static u32 b_rnd(void);
static u32 b_same(const u8 *x, const u8 *y, u32 n);
static void b_tmpfile(void);
static void b_result(void);
static void b_loop(u32 mode, u32 n);
static void b_fill(u8 *p, u32 n, u32 m, u32 c);


void index_passes(void)
{
    u8 *buf = BBUF;
    u32 i;
    B.ctx = V32(0x10);
    B.h = V32(0x0c);
    B.seed = 12345;
    B.ok_tmp = 2;
    b_seek(B.h, 0);
    b_loop(0, 16384);                                                /* T1 */
    b_loop(1, 2048);                                                 /* T2 */
    b_loop(2, 2048);                                                 /* T3 */
    b_seek(B.h, 3 * 8192);                                           /* T4 */
    for (i = 0; i < 16; i++) b_read(B.h, buf + 0x8000 + i * 512, 512);
    b_fill(buf, 8192, 0, 0xaa);
    b_seek(B.h, 0);
    b_loop(3, 1024);
    b_fill(buf, 512, 7, 3);                                          /* T5 */
    b_seek(B.h, WR_BASE);
    b_loop(4, 16384);
    b_fill(buf, 8192, 13, 5);                                        /* T6 */
    b_seek(B.h, WR_BASE);
    b_loop(5, 1024);
    b_seek(B.h, WR_BASE + 3 * 8192);
    for (i = 0; i < 16; i++) b_read(B.h, buf + 0x8000 + i * 512, 512);
    B.ok_w8 = b_same(buf, buf + 0x8000, 8192);
    b_tmpfile();                                                     /* T7 */
    PROGRESS(0xff);
    b_result();
    /* leave an empty library: no songs, no sections, so the player has nothing to browse */
    for (i = 0x40; i < 0x9c; i += 2) V16(i) = 0;
}

static void b_loop(u32 mode, u32 n)              /* n operations of one kind, pumping the UI */
{
    u8 *buf = BBUF;
    u32 i;
    for (i = 0; i < n; i++) {
        switch (mode) {
        case 0: b_read(B.h, buf, 512); break;                             /* sequential */
        case 1: b_seek(B.h, (b_rnd() % (FILE_LEN >> 9)) << 9); b_read(B.h, buf, 512); break;
        case 2: b_seek(B.h, i << 12); b_read(B.h, buf, 512); break;
        case 3: { u32 got = b_read(B.h, buf, 8192);
                  if (i == 3) { B.r8 = got; B.ok_r8 = b_same(buf, buf + 0x8000, 8192); } break; }
        case 4: b_write(B.h, buf, 512); break;
        case 5: { u32 got = b_write(B.h, buf, 8192); if (i == 3) B.w8 = got; break; }
        }
        if ((i & 63) == 0) PUMP();
    }
    PROGRESS(0xff);
}

static void b_fill(u8 *p, u32 n, u32 m, u32 c) { while (n--) { *p++ = (u8)(m * n + c); } }

static void b_seek(u32 h, u32 pos) { FS_OP(B.ctx, pos, 0, h, 0xb); }
static u32 b_read(u32 h, void *dst, u32 n) { return FS_OP(B.ctx, (u32)dst, n, h, 0xd); }
static u32 b_write(u32 h, const void *src, u32 n) { return FS_OP(B.ctx, (u32)src, n, h, 0xe); }
static u32 b_rnd(void) { B.seed = B.seed * 1103515245u + 12345u; return B.seed >> 8; }
static u32 b_same(const u8 *x, const u8 *y, u32 n) { while (n--) if (*x++ != *y++) return 0; return 1; }

/* a file name as FUN_40194c00 makes it: BOM then UTF-16LE, in the app's name buffer */
static void b_name(const char *s)
{
    u8 *p = (u8 *)(RAMB + 0x15538u);
    *p++ = 0xff; *p++ = 0xfe;
    while (*s) { *p++ = *s++; *p++ = 0; }
    *p++ = 0; *p = 0;
}

static void b_tmpfile(void)                      /* as FUN_40194c78 creates MUSIC.LIB */
{
    u8 *buf = BBUF;
    u32 i, h, ok = 1;
    FS_OP(B.ctx, 0x3a, 0, 0, 0);                 /* the root directory: the scan left it elsewhere */
    b_name("MUSIC.TMP");
    h = FS_OP(B.ctx, RAMB + 0x15538u, TMP_LEN, 0, 6);
    if (h) {
        FS_OP(B.ctx, 2, h, 0, 0x13);
        FS_OP(B.ctx, h, 0, 0, 7);
        h = FS_OP(B.ctx, RAMB + 0x15538u, 0, 0, 5);
    }
    B.h2 = h;
    if (!h) { B.ok_tmp = 0; return; }
    for (i = 0; i < 2048; i++) {
        b_seek(h, (b_rnd() % (TMP_LEN >> 10)) << 9);
        b_read(h, buf, 512);
        if ((i & 63) == 0) PUMP();
    }
    /* write 4 KB through the new handle, read it back; meanwhile the library handle reads */
    b_fill(buf, 4096, 3, 1);
    b_seek(h, 0x10000);
    b_write(h, buf, 2048);
    b_seek(B.h, 0x200);
    b_read(B.h, buf + 0x8000, 512);
    b_write(h, buf + 2048, 2048);
    b_seek(h, 0x10000);
    for (i = 0; i < 8; i++) b_read(h, buf + 0x4000 + i * 512, 512);
    ok = b_same(buf, buf + 0x4000, 4096);
    FS_OP(B.ctx, h, 0, 0, 7);
    b_name("MUSIC.TMP");
    FS_OP(B.ctx, RAMB + 0x15538u, 1, 0, 8);
    b_name("MUSIC   TMP");
    FS_OP(B.ctx, RAMB + 0x15538u, 1, 0, 8);
    b_name("MUSIC.TMP");
    h = FS_OP(B.ctx, RAMB + 0x15538u, 0, 0, 5);
    B.ok_del = !h;
    if (h) FS_OP(B.ctx, h, 0, 0, 7);
    B.ok_tmp = ok;
}

static u8 *b_num(u8 *p, u32 v)
{
    u8 t[10]; u32 i = 0;
    do { t[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = t[--i];
    return p;
}
static u8 *b_str(u8 *p, const char *s) { while (*s) *p++ = *s++; return p; }
static u8 *b_hex(u8 *p, const u8 *s, u32 n)
{
    const char *h = "0123456789abcdef";
    while (n--) { *p++ = h[*s >> 4]; *p++ = h[*s & 15]; s++; }
    return p;
}

static void b_result(void)
{
    u8 *buf = BBUF, *p;
    u32 i;
    b_seek(B.h, 0x200);
    for (i = 0; i < 4; i++) b_read(B.h, buf + i * 512, 512);       /* songs 0 to 2 (660 bytes each) */
    for (i = 0; i < 0x40; i++) buf[i] = 0;
    p = b_str(buf, "BENCH R8 ");
    p = b_str(p, B.ok_r8 ? "ok" : "BAD");
    p = b_str(p, " W8 ");
    p = b_str(p, B.ok_w8 ? "ok" : "BAD");
    p = b_str(p, " T ");
    p = b_str(p, B.ok_tmp == 2 ? "none" : B.ok_tmp ? "ok" : "BAD");
    p = b_str(p, " D ");
    p = b_str(p, B.ok_del ? "ok" : "BAD");
    p = b_str(p, " r");
    p = b_num(p, B.r8);
    p = b_str(p, " w");
    p = b_num(p, B.w8);
    p = b_str(p, " t");
    p = b_num(p, B.h2);
#ifndef TEST
    /* the directory walker, from the root, for the first two entries */
    FS_OP(B.ctx, 0x3a, 0, 0, 0);
    for (i = 0; i < 2; i++) {
        u32 e = 0;
        u8 *r = buf + 660 * (i + 1);
        u32 k;
        if (!WALKER(&e)) break;
        B.ent[i] = e;
        for (k = 0; k < 0x88; k++) r[k] = 0;
        p = b_hex(r, (const u8 *)(e - 32), 32);               /* title: 32 bytes before */
        p = b_hex(r + 0x58, (const u8 *)e, 24);                /* album: 24 bytes from it */
        p = b_str(r + 0x40, "p");                              /* artist: the pointer */
        p = b_hex(p, (const u8 *)&B.ent[i], 4);
    }
#endif
    b_seek(B.h, 0x200);
    for (i = 0; i < 4; i++) b_write(B.h, buf + i * 512, 512);
}
