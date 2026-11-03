/* Diagnostic build: replay the library passes' file operations with self-describing data.
 *
 * Takes the place of index.c in the blob (build.sh --library-replay).  After the 100-file scan
 * it reads IOTEST.OPS from the card root (made by replay.py from a -DTRACE_OPS harness run of
 * the real passes), creates MUSIC.TMP as the passes do, and performs every seek, read and
 * write of the stream on the same two files.  A write fills its bytes with a pattern that
 * names the file and the byte offset (the word at aligned offset w of file f is w ^ KEY[f]);
 * a read flagged in the stream (all its bytes written earlier) is checked against the pattern
 * at the position the stream says the read starts at.  So the kernel's handling of the exact
 * sequence the passes use (sub-sector positions and lengths, two handles interleaved) shows up
 * as wrong bytes, with the stream index of the first wrong reads, the position asked for and
 * what was found.  replay.py check compares the files left on the card with the same model.
 *
 * Results: text in sector 1 of MUSIC.LIB ("REPLAY ops=<n> tmp=<handle> ovr=<reads that wrote
 * past their buffer> short=<ops that returned less than asked>@<first> bad=<wrong reads> ...
 * then for the first four wrong reads: op index, position, byte offset of the first wrong
 * byte, the word found there and the one expected), and one byte per op at 10 MB of MUSIC.LIB
 * as a bitmap: a set bit is an op with wrong bytes or a short transfer.  MUSIC.TMP is left
 * on the card.  The op stream is replay.py's OPS3 format: 6 bytes per op, up to 13,000 ops,
 * read from IOTEST.OPS in the root, or, when that is missing or fails its checksum, from
 * MUSIC.LIB at 12 MB (put there with dd conv=notrunc; the library's directory entry does not
 * change, so a stale directory on the player cannot point at an old copy).  The result text
 * starts with where the stream came from and what the loader saw.
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
#define MEM      0xbfc00000u                                         /* uncached alias */
#define FS_OP    ((u32 (*)(u32, u32, u32, u32, u32))0xbfc1640du)
#define PUMP     ((u32 (*)(void))0xbfc165e5u)
#define PROGRESS ((u32 (*)(u32))0x401d4cb1u)
#endif
#define V32(o) (*(volatile u32 *)(RAMB + 0x15400u + (o)))
#define V16(o) (*(volatile u16 *)(RAMB + 0x15400u + (o)))

#define OPS      ((volatile u16 *)(MEM + 0x22000u))   /* the op stream, up to 80 KB */
#define OPS_MAX  0x14000u
#define WBUF     ((u8 *)(MEM + 0x36000u))             /* 8 KB write buffer */
#define RBUF     ((u8 *)(MEM + 0x38000u))             /* 8 KB read buffer, 1 KB canary after it */
#define TXT      ((u8 *)(MEM + 0x3a400u))             /* the result text, one sector */
#define MAP      ((u8 *)(MEM + 0x3a800u))             /* one bit per op, 2 KB */
#define MAP_POS  0xa00000u                             /* where the map goes in MUSIC.LIB */
#define LIB_OPS  0xc00000u                             /* the second copy of the stream in MUSIC.LIB */
#define BLK      8192u
#define NFIRST   4

typedef struct { u32 op, pos, at, got, exp; } first_t;
typedef struct {
    u32 ctx, hL, hT, n, tmpsize, ops_ok;
    u32 src, oh, ogot, ohdr[2], lhdr[2], sum, want;   /* loader diagnostics */
    u32 pos[3];                                  /* tracked position per handle */
    u32 ovr, nshort, firstshort, bad, nfirst;
    first_t first[NFIRST];
} st_t;
#define S (*(volatile st_t *)(RAMB + 0x3b000u))

/* index_passes must come first: the stock jumps to the start of the blob */
static void name(const char *s);
static u32 patw(u32 f, u32 w);
static u8 patb(u32 f, u32 o);
static u32 load_ops(void);
static void run(void);
static void check(u32 i, u32 h, u32 p, u32 n);
static void result(void);

void index_passes(void)
{
    u32 i, h;
    S.ctx = V32(0x10);
    S.hL = V32(0x0c);
    S.hT = 0; S.n = 0; S.ovr = 0; S.nshort = 0; S.firstshort = 0xffffffffu; S.bad = 0; S.nfirst = 0;
    S.ops_ok = load_ops();
    for (i = 0; i < 1024; i++) RBUF[BLK + i] = 0xc5;                 /* the canary */
    if (S.ops_ok) {
        FS_OP(S.ctx, 0x3a, 0, 0, 0);                                  /* the root */
        name("MUSIC.TMP"); FS_OP(S.ctx, RAMB + 0x15538u, 1, 0, 8);    /* no leftover: as index.c */
        name("MUSIC   TMP"); FS_OP(S.ctx, RAMB + 0x15538u, 1, 0, 8);
        name("MUSIC.TMP");
        h = FS_OP(S.ctx, RAMB + 0x15538u, S.tmpsize, 0, 6);
        if (h) { FS_OP(S.ctx, 2, h, 0, 0x13); FS_OP(S.ctx, h, 0, 0, 7); h = FS_OP(S.ctx, RAMB + 0x15538u, 0, 0, 5); }
        S.hT = h;
        if (h) { run(); FS_OP(S.ctx, h, 0, 0, 7); }
    }
    result();
    for (i = 0x40; i < 0x9c; i += 2) V16(i) = 0;  /* an empty library */
}

static u32 patw(u32 f, u32 w) { return w ^ (f == 1 ? 0x4c490000u : 0x544d0000u); }
static u8 patb(u32 f, u32 o) { return (u8)(patw(f, o & ~3u) >> (8 * (o & 3))); }

static u32 ops_valid(u32 got)                     /* header, size and checksum of what is in OPS */
{
    u32 i, sum = 0, n;
    if (OPS[0] != 0x504fu || OPS[1] != 0x3353u) return 0;   /* "OPS3" */
    n = OPS[2] | OPS[3] << 16;
    S.want = OPS[6] | OPS[7] << 16;
    if (got < 16 + 6 * n || 16 + 6 * n > OPS_MAX) return 0;
    for (i = 8; i < 8 + 3 * n; i++) sum += OPS[i];
    S.sum = sum;
    if (sum != S.want) return 0;
    S.n = n; S.tmpsize = OPS[4] | OPS[5] << 16;
    return 1;
}

static u32 load_ops(void)                        /* IOTEST.OPS from the root, else MUSIC.LIB at 12 MB */
{
    u32 h, got = 0, k;
    u8 *p = (u8 *)OPS;
    S.src = 0; S.oh = 0; S.ogot = 0; S.ohdr[0] = S.ohdr[1] = S.lhdr[0] = S.lhdr[1] = 0; S.sum = S.want = 0;
    FS_OP(S.ctx, 0x3a, 0, 0, 0);
    name("IOTEST.OPS");
    h = FS_OP(S.ctx, RAMB + 0x15538u, 0, 0, 5);
    S.oh = h;
    if (h) {
        FS_OP(S.ctx, 0, 0, h, 0xb);
        while (got < OPS_MAX) {
            k = FS_OP(S.ctx, (u32)(p + got), BLK, h, 0xd);
            if (k == 0 || k > BLK) break;
            got += k;
        }
        FS_OP(S.ctx, h, 0, 0, 7);
        S.ogot = got; S.ohdr[0] = OPS[0] | OPS[1] << 16; S.ohdr[1] = OPS[2] | OPS[3] << 16;
        if (ops_valid(got)) { S.src = 1; return 1; }
    }
    /* the copy in the library, in 4 KB pieces that stay inside their block */
    for (got = 0; got < OPS_MAX; got += 4096) {
        FS_OP(S.ctx, LIB_OPS + got, 0, S.hL, 0xb);
        k = FS_OP(S.ctx, (u32)(p + got), 4096, S.hL, 0xd);
        if (k != 4096) break;
    }
    S.lhdr[0] = OPS[0] | OPS[1] << 16; S.lhdr[1] = OPS[2] | OPS[3] << 16;
    if (ops_valid(got)) { S.src = 2; return 1; }
    return 0;
}

static void run(void)
{
    u32 i, j, k;
    S.pos[1] = S.pos[2] = 0xffffffffu;
    for (i = 0; i < 2048; i++) MAP[i] = 0;
    for (i = 0; i < S.n; i++) {
        u32 w0 = OPS[8 + 3 * i], p = OPS[9 + 3 * i] | OPS[10 + 3 * i] << 16;
        u32 code = w0 & 3, hh = (w0 >> 2 & 1) + 1, flag = w0 >> 3 & 1, n = (w0 >> 4) << 9;
        u32 h = hh == 1 ? S.hL : S.hT;
        if (code == 0) {
            FS_OP(S.ctx, p, 0, h, 0xb);
            S.pos[hh] = p;
        } else if (code == 1) {
            k = FS_OP(S.ctx, (u32)RBUF, n, h, 0xd);
            if (k != n) { MAP[i >> 3] |= 1 << (i & 7); if (S.firstshort == 0xffffffffu) S.firstshort = i; S.nshort++; }
            for (j = 0; j < 1024; j++) if (RBUF[BLK + j] != 0xc5) { S.ovr++; RBUF[BLK + j] = 0xc5; break; }
            if (flag) check(i, hh, p, n);
            S.pos[hh] += n;
        } else {
            for (j = 0; j < n; j++) WBUF[j] = patb(hh, p + j);
            k = FS_OP(S.ctx, (u32)WBUF, n, h, 0xe);
            if (k != n) { MAP[i >> 3] |= 1 << (i & 7); if (S.firstshort == 0xffffffffu) S.firstshort = i; S.nshort++; }
            S.pos[hh] += n;
        }
        if ((i & 15) == 0) PUMP();
        if (i % (S.n / 8 + 1) == 0) PROGRESS(0xff);
    }
}

static void check(u32 i, u32 h, u32 p, u32 n)   /* RBUF against the pattern of file h at p */
{
    u32 j;
    for (j = 0; j < n; j++) {
        if (RBUF[j] != patb(h, p + j)) {
            u32 a = j & ~3u;
            S.bad++;
            MAP[i >> 3] |= 1 << (i & 7);
            if (S.nfirst < NFIRST) {
                volatile first_t *f = &S.first[S.nfirst++];
                f->op = i; f->pos = p; f->at = j;
                f->got = RBUF[a] | RBUF[a + 1] << 8 | RBUF[a + 2] << 16 | (u32)RBUF[a + 3] << 24;
                f->exp = patw(h, (p + a) & ~3u);
            }
            return;
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

static u8 *num(u8 *p, u32 v)
{
    u8 t[10]; u32 i = 0;
    do { t[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) *p++ = t[--i];
    return p;
}
static u8 *hex(u8 *p, u32 v)
{
    const char *h = "0123456789abcdef";
    u32 i;
    for (i = 0; i < 8; i++) *p++ = h[(v >> (28 - 4 * i)) & 15];
    return p;
}
static u8 *str(u8 *p, const char *s) { while (*s) *p++ = *s++; return p; }

static void result(void)
{
    u8 *p = TXT;
    u32 i;
    for (i = 0; i < 512; i++) TXT[i] = 0;
    p = str(p, "REPLAY src=");  p = num(p, S.src);
    p = str(p, " ops h=");       p = hex(p, S.oh);
    p = str(p, " got=");         p = num(p, S.ogot);
    p = str(p, " hdr=");         p = hex(p, S.ohdr[0]); *p++ = ' '; p = hex(p, S.ohdr[1]);
    p = str(p, " libhdr=");      p = hex(p, S.lhdr[0]); *p++ = ' '; p = hex(p, S.lhdr[1]);
    p = str(p, " sum=");         p = hex(p, S.sum); *p++ = '/'; p = hex(p, S.want);
    p = str(p, " ops=");         p = num(p, S.n);
    p = str(p, S.ops_ok ? " loaded" : " NOT LOADED");
    p = str(p, " tmp=");         p = hex(p, S.hT);
    p = str(p, " ovr=");         p = num(p, S.ovr);
    p = str(p, " short=");       p = num(p, S.nshort); *p++ = '@'; p = num(p, S.firstshort);
    p = str(p, " bad=");         p = num(p, S.bad);
    for (i = 0; i < S.nfirst; i++) {
        p = str(p, " | op "); p = num(p, S.first[i].op);
        p = str(p, " pos ");  p = hex(p, S.first[i].pos);
        p = str(p, " at ");   p = num(p, S.first[i].at);
        p = str(p, " got ");  p = hex(p, S.first[i].got);
        p = str(p, " exp ");  p = hex(p, S.first[i].exp);
    }
    FS_OP(S.ctx, 0x200, 0, S.hL, 0xb);
    FS_OP(S.ctx, (u32)TXT, 512, S.hL, 0xe);
    if (S.n) {
        FS_OP(S.ctx, MAP_POS, 0, S.hL, 0xb);
        FS_OP(S.ctx, (u32)MAP, (S.n / 8 + 511) & ~511u, S.hL, 0xe);
    }
#ifdef TEST
    V16(0x0a) = (MAP_POS >> 9) + ((S.n / 8 + 511) >> 9);  /* the harness dumps the library up to the sector counter */
#endif
}
