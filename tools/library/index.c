/* File-backed MUSIC.LIB index passes for the MECHEN M30 (Actions ATJ2127) library builder.
 *
 * Replaces FUN_400d5290 in playlist.ap: after the scan has written N records, build the
 * All songs, Album, Artist, and Genre sections with the same tables and nodes as the stock
 * passes, but with the per-song data in files instead of RAM, so N is not limited by the
 * 120 KB the builder owns.  See docs/library-builder.md.
 *
 * A kernel file call costs about 10 ms on the player, so everything here is arranged to
 * make as few as possible: the scratch data lives in a second file, MUSIC.TMP, with its own
 * handle, so the library is only read sequentially and appended to; every stream has a
 * large buffer; a seek is issued only when a handle's position is not already right; the
 * merge is a single pass of up to 32 runs; and reads and writes go in 4 KB pieces.  If
 * MUSIC.TMP cannot be created, the scratch area goes at the end of MUSIC.LIB instead.
 *
 * Every file call starts on a sector boundary, moves a whole number of sectors, and stays
 * inside one 4 KB block.  The player's kernel gets both of the other cases wrong: a call
 * that starts or ends inside a sector returns or stores the wrong bytes, and a call that
 * crosses a cluster boundary in a file with a FAT chain (MUSIC.LIB, when the card's free
 * space is fragmented) stops advancing along the chain and poisons the handle's position.
 * So the sort runs are padded to sector boundaries (the run pitch), readers fetch aligned
 * windows and skip into them, and writers pad their last sector.
 *
 * Per section: extract one key record per song (the converted name, the secondary sort
 * fields, and the song index), sort them with in-RAM runs and the merge, then walk the
 * sorted stream once for the group run lengths and the sort order, write the level nodes
 * from the order, and build the per-song tables (inverse rank, node index, artist group) in
 * 8 KB chunks.  The names of all four sections are converted in one pass over the records.
 *
 * Built with -mips16 -Os (see build.sh), with TEST defined for the QEMU harness.
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
#define RAMB     0x9fc00000u                                        /* RAM through the data cache */
#define MEM      0xbfc00000u                                        /* the same RAM, uncached */
#define FS_OP    ((u32 (*)(u32, u32, u32, u32, u32))0xbfc1640du)   /* module 0x11: file system */
#define PUMP     ((u32 (*)(void))0xbfc165e5u)                       /* message pump */
#define PROGRESS ((u32 (*)(u32))0x401d4cb1u)                        /* progress bar */
#endif
#define CONVERT  ((u32 (*)(u32, u32, u32))0x480145ddu)              /* FUN_480145dc: name, handle, width */

/* app variables at 0x9fc15400 */
#define V8(o)  (*(volatile u8 *)(RAMB + 0x15400u + (o)))
#define V16(o) (*(volatile u16 *)(RAMB + 0x15400u + (o)))
#define V32(o) (*(volatile u32 *)(RAMB + 0x15400u + (o)))
#define NAMEBUF ((u8 *)(RAMB + 0x15538u))           /* file names for open/create, and CONVERT's output */
#define CONVBUF ((const u8 *)(RAMB + 0x15538u))

/* the builder's RAM: a 96 KB arena laid out per phase, and small state above it.  Every
 * buffer that a file call reads or fills is used through the uncached alias (a precaution
 * from when stale data in a device library was blamed on the cache; the real cause was the
 * kernel's file I/O, see docs/library-builder.md).  The state that only this code touches
 * stays cached. */
#define ARENA    ((u8 *)(MEM + 0x22000u))
#define KB(n)    ((n) * 1024u)
#define SLOTS    ((u8 *)(RAMB + 0x3a000u))      /* 32 x 80: the current record of each merge input */
#define SLOT_SZ  80u
#define RDRS     ((rd_t *)(RAMB + 0x3aa00u))    /* the merge readers */
#define RECMAX   76u                             /* largest key record, rounded */
#define MAXK     32u
#define CHUNK_N  4096u
#define BIG      8192u
#define BLOCK    4096u                           /* no call crosses a boundary of this: the smallest cluster expected */

typedef struct { u32 h, pos, end, n, i, cap; u8 *buf; } rd_t;
typedef struct { u32 h, pos, i, cap; u8 *buf; } wr_t;
typedef struct {
    u32 N, ctx, hL, hT, T0, rio, wio, cur[2];
    u32 S0, S1, K[4], L[3], ORD, RANK, AGRP, GGRP, END;     /* scratch areas, offsets from T0 */
} globals_t;
#define G (*(volatile globals_t *)(RAMB + 0x3ae00u))
#define N G.N
#define T(x) (G.T0 + (x))

static void pass(u32 sec);
static void setup(void);
static void teardown(void);
static void zero(u8 *p, u32 n);
static u32 align(u32 x);

void index_passes(void)
{
    u32 sec;
    N = V16(0x40);
    V16(0xb4) = N;
    if (N == 0) return;
    setup();
    for (sec = 0; sec < 4; sec++) {
        pass(sec);
        PUMP();
        PROGRESS(0xff);
    }
    teardown();
}

static void zero(u8 *p, u32 n) { while (n--) *p++ = 0; }
static void copy(u8 *d, const u8 *s, u32 n) { while (n--) *d++ = *s++; }
static u32 rd16(const u8 *p) { return p[0] | (p[1] << 8); }
static void wr16(u8 *p, u32 v) { p[0] = v; p[1] = v >> 8; }
static u32 align(u32 x) { return (x + 511u) & ~511u; }

/* ---- file access: seek only when the handle is not already there ---- */
static volatile u32 *curp(u32 h) { return &G.cur[h == G.hL ? 0 : 1]; }
static void fs_seek(u32 h, u32 pos)
{
    volatile u32 *c = curp(h);
    if (*c != pos) { FS_OP(G.ctx, pos, 0, h, 0xb); *c = pos; }
}
static void fs_io(u32 h, u32 pos, u8 *p, u32 n, u32 op, u32 piece)
{
    fs_seek(h, pos);
    while (n) {
        u32 k = BLOCK - (pos & (BLOCK - 1));
        if (k > piece) k = piece;
        if (k > n) k = n;
        FS_OP(G.ctx, (u32)p, k, h, op);
        p += k; n -= k; pos += k; *curp(h) += k;
    }
}
static void fs_read(u32 h, u32 pos, void *dst, u32 n) { fs_io(h, pos, (u8 *)dst, n, 0xd, G.rio); }
static void fs_write(u32 h, u32 pos, const void *src, u32 n) { fs_io(h, pos, (u8 *)src, n, 0xe, G.wio); }
static void lib_append(const void *buf, u32 n)              /* at the sector counter, as the stock append */
{
    n = align(n);
    fs_write(G.hL, (u32)V16(0x0a) << 9, buf, n);
    V16(0x0a) += n >> 9;
}

/* ---- buffered sequential readers and writers ---- */
static void rd_open(rd_t *r, u32 h, u8 *buf, u32 cap, u32 pos, u32 end)
{
    r->h = h; r->buf = buf; r->cap = cap; r->pos = pos; r->end = end; r->n = r->i = 0;
}
static u32 rd_left(const rd_t *r) { return (r->end - r->pos) + (r->n - r->i); }
static u32 rd_u8(rd_t *r)
{
    if (r->i >= r->n) {                         /* refill with an aligned window and skip into it */
        u32 ws = r->pos & ~511u, left = r->end - ws, len;
        if (r->pos >= r->end) return 0;
        len = left > r->cap ? r->cap : align(left);
        fs_read(r->h, ws, r->buf, len);
        r->i = r->pos - ws;
        r->n = left < len ? left : len;
        r->pos = ws + r->n;
    }
    return r->buf[r->i++];
}
static u32 rd_u16(rd_t *r) { u32 lo = rd_u8(r); return lo | (rd_u8(r) << 8); }
static void rd_bytes(rd_t *r, u8 *dst, u32 n) { while (n--) *dst++ = rd_u8(r); }
static void rd_skip(rd_t *r, u32 n) { while (n--) rd_u8(r); }

static void wr_open(wr_t *w, u32 h, u8 *buf, u32 cap, u32 pos) { w->h = h; w->buf = buf; w->cap = cap; w->pos = pos; w->i = 0; }
static void wr_close(wr_t *w)
{
    if (w->i) {
        u32 n = align(w->i);
        zero(w->buf + w->i, n - w->i);
        fs_write(w->h, w->pos, w->buf, n);
        w->pos += n; w->i = 0;
    }
}
static void wr_u8(wr_t *w, u32 b) { w->buf[w->i++] = b; if (w->i == w->cap) wr_close(w); }
static void wr_u16(wr_t *w, u32 v) { wr_u8(w, v); wr_u8(w, v >> 8); }
static void wr_bytes(wr_t *w, const u8 *s, u32 n) { while (n--) wr_u8(w, *s++); }

/* ---- the scratch file ---- */
static void name(const char *s)                 /* as FUN_40194c00: BOM then UTF-16LE */
{
    u8 *p = NAMEBUF;
    *p++ = 0xff; *p++ = 0xfe;
    while (*s) { *p++ = *s++; *p++ = 0; }
    *p++ = 0; *p = 0;
}
#ifndef PROBE
static void tmp_delete(void)                    /* by the long name (works on the exFAT card) and
                                                   the 8.3 name (FAT32) */
{
    name("MUSIC.TMP");
    FS_OP(G.ctx, (u32)NAMEBUF, 1, 0, 8);
    name("MUSIC   TMP");
    FS_OP(G.ctx, (u32)NAMEBUF, 1, 0, 8);
}
#endif

static void setup(void)
{
    u32 i, h, sz;
    G.ctx = V32(0x10);
    G.hL = V32(0x0c);
    G.cur[0] = G.cur[1] = 0xffffffffu;
    G.rio = G.wio = BLOCK;                      /* one block per call: verified on the player */
    /* scratch areas */
    sz = 0;
    G.S0 = sz; sz += align(RECMAX * N) + KB(1);  /* with room for the run padding */
    G.S1 = sz; sz += align(RECMAX * N) + KB(1);
    G.K[1] = sz; sz += align(50 * N);
    G.K[2] = sz; sz += align(26 * N);
    G.K[3] = sz; sz += align(26 * N);
    for (i = 0; i < 3; i++) { G.L[i] = sz; sz += align(2 * N + 2); }
    G.ORD = sz; sz += align(2 * N);
    G.RANK = sz; sz += align(2 * N);
    G.AGRP = sz; sz += align(2 * N);
    G.GGRP = sz; sz += align(2 * N);
    G.END = sz;
    /* MUSIC.TMP with its own handle, or else the tail of the library.  A leftover from an
     * interrupted update is deleted first: the kernel cannot extend a file past the size it
     * was created with, and a leftover may be smaller than this card needs */
    FS_OP(G.ctx, 0x3a, 0, 0, 0);                /* the root directory */
#ifndef PROBE
    tmp_delete();
#endif
    name("MUSIC.TMP");
    h = FS_OP(G.ctx, (u32)NAMEBUF, sz + 0x10000, 0, 6);
    if (h) {
        FS_OP(G.ctx, 2, h, 0, 0x13);
        FS_OP(G.ctx, h, 0, 0, 7);
        h = FS_OP(G.ctx, (u32)NAMEBUF, 0, 0, 5);
    }
    G.hT = h ? h : G.hL;
    G.T0 = h ? 0 : align(0x200 + 384 * N);       /* after the records, the tables, and the nodes */
}

static void teardown(void)
{
#ifdef PROBE
    /* diagnostic build (build.sh --library-probe): MUSIC.TMP stays on the card, with the globals
     * in a sector after its scratch areas, for comparing against the harness */
    u32 i; u8 *a = ARENA; const volatile u8 *g = (const volatile u8 *)&G;
    zero(a, 512);
    for (i = 0; i < sizeof G; i++) a[i] = g[i];
    fs_write(G.hT, T(G.END), a, 512);
    if (G.hT != G.hL) FS_OP(G.ctx, G.hT, 0, 0, 7);
#else
    if (G.hT != G.hL) { FS_OP(G.ctx, G.hT, 0, 0, 7); tmp_delete(); }
#endif
}

/* ---- key records: name[W] | track u8 | pad | rank u16 | group1 u16 | group2 u16 | song u16 ---- */
static int cmprec(const u8 *x, const u8 *y, u32 W, u32 sec)
{
    u32 i, a, b;
    for (i = 0; i < W; i++) {                   /* as FUN_71812548: ASCII upper-cased, stops at a shared NUL */
        a = x[i]; b = y[i];
        if (a - 'a' < 26) a -= 32;
        if (b - 'a' < 26) b -= 32;
        if (a != b) return a < b ? -1 : 1;
        if (!a) break;
    }
    if (sec == 1) {                             /* inside an album: track number as a signed byte, as FUN_718126c0 */
        int ta = (signed char)x[W], tb = (signed char)y[W];
        if (ta != tb) return ta < tb ? -1 : 1;
    }
    if (sec) {                                  /* then the rank in the previous section's order */
        a = rd16(x + W + 2); b = rd16(y + W + 2);
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;                                   /* equal: the stable sort keeps song order */
}

/* 1. extract.  Section 0 reads the records once and also writes the converted album, artist
 * and genre names (with the track byte) to K1..K3; later sections read those back. */
static void extract(u32 sec, u32 W, u32 reclen, u32 cap, u32 pitch)
{
    wr_t w, k[4]; rd_t ks, rank, g1, g2;
    u32 i;
    u8 tail[10];
    wr_open(&w, G.hT, ARENA + KB(48), KB(16), T(G.S0));
    if (sec == 0) {                             /* records at 0, K writers at 64, 72, 80 KB */
        for (i = 1; i < 4; i++) wr_open(&k[i], G.hT, ARENA + KB(56 + 8 * i), KB(8), T(G.K[i]));
    } else {
        rd_open(&ks, G.hT, ARENA, KB(16), T(G.K[sec]), T(G.K[sec]) + (W + 2) * N);
        rd_open(&rank, G.hT, ARENA + KB(16), KB(8), T(G.RANK), T(G.RANK) + 2 * N);
        if (sec == 2) rd_open(&g1, G.hT, ARENA + KB(24), KB(8), T(G.AGRP), T(G.AGRP) + 2 * N);
        if (sec == 3) {
            rd_open(&g1, G.hT, ARENA + KB(24), KB(8), T(G.GGRP), T(G.GGRP) + 2 * N);
            rd_open(&g2, G.hT, ARENA + KB(32), KB(8), T(G.AGRP), T(G.AGRP) + 2 * N);
        }
    }
    for (i = 0; i < N; i++) {
        zero(tail, sizeof tail);
        if (i && i % cap == 0) {                /* the next run, on its own sector */
            wr_close(&w);
            wr_open(&w, G.hT, ARENA + KB(48), KB(16), T(G.S0) + (i / cap) * pitch);
        }
        if (sec == 0) {
            const u8 *rec = ARENA + (i & 63) * 256;
            u32 f;
            if ((i & 63) == 0) {
                u32 n = N - i; if (n > 64) n = 64;
                fs_read(G.hL, 0x200 + i * 256, ARENA, align(n * 256));
            }
            for (f = 0; f < 4; f++) {           /* title, album, artist, genre */
                u32 fo = f == 0 ? 0x00 : f == 1 ? 0x58 : f == 2 ? 0x40 : 0x88, fw = f == 0 ? 0x40 : f == 1 ? 0x30 : 0x18;
                CONVERT((u32)(rec + fo), V8(0x08), fw);
                if (f == 0) wr_bytes(&w, CONVBUF, fw);
                else { wr_bytes(&k[f], CONVBUF, fw); wr_u8(&k[f], rec[0xf2]); wr_u8(&k[f], 0); }
            }
            tail[0] = rec[0xf2];
        } else {
            u8 t[2];
            rd_bytes(&ks, ARENA + KB(40), W);   /* the name, then the track byte and its pad */
            wr_bytes(&w, ARENA + KB(40), W);
            rd_bytes(&ks, t, 2);
            tail[0] = t[0];
            wr16(tail + 2, rd_u16(&rank));
            if (sec >= 2) wr16(tail + 4, rd_u16(&g1));
            if (sec == 3) wr16(tail + 6, rd_u16(&g2));
        }
        wr16(tail + 8, i);
        wr_bytes(&w, tail, reclen - W);
        if ((i & 255) == 0) PUMP();
    }
    wr_close(&w);
    if (sec == 0) for (i = 1; i < 4; i++) wr_close(&k[i]);
}

/* 2. runs: sort the runs of S0 (cap records each, one per pitch) in RAM (stable merge sort
 * on indexes) to the same places in S1 */
static void make_runs(u32 sec, u32 W, u32 reclen, u32 cap, u32 pitch)
{
    u8 *RUNBUF = ARENA;
    u32 start, r;
    for (start = 0, r = 0; start < N; start += cap, r++) {
        u32 cnt = N - start, i, width;
        u16 *a = (u16 *)(ARENA + KB(48)), *b = (u16 *)(ARENA + KB(52)), *t;
        wr_t w;
        if (cnt > cap) cnt = cap;
        fs_read(G.hT, T(G.S0) + r * pitch, RUNBUF, align(cnt * reclen));
        PUMP();
        for (i = 0; i < cnt; i++) a[i] = i;
        for (width = 1; width < cnt; width *= 2) {
            for (i = 0; i < cnt; i += 2 * width) {
                u32 l = i, lend = i + width, r = i + width, rend = i + 2 * width, k = i;
                if (lend > cnt) lend = cnt;
                if (rend > cnt) rend = cnt;
                while (l < lend && r < rend)
                    b[k++] = cmprec(RUNBUF + a[r] * reclen, RUNBUF + a[l] * reclen, W, sec) < 0 ? a[r++] : a[l++];
                while (l < lend) b[k++] = a[l++];
                while (r < rend) b[k++] = a[r++];
            }
            t = a; a = b; b = t;
        }
        wr_open(&w, G.hT, ARENA + KB(56), KB(16), T(G.S1) + r * pitch);
        for (i = 0; i < cnt; i++) wr_bytes(&w, RUNBUF + a[i] * reclen, reclen);
        wr_close(&w);
    }
}

/* 3. one merge pass: every MAXK runs of runlen records (pitch bytes apart) in src become
 * one run in dst, MAXK pitches apart */
static void merge_pass(u32 sec, u32 W, u32 reclen, u32 src, u32 dst, u32 runlen, u32 pitch)
{
    u32 nruns = (N + runlen - 1) / runlen, g;
    rd_t *in = RDRS;
    for (g = 0; g < nruns; g += MAXK) {
        u32 k, nk = nruns - g, active = 0, cnt;
        wr_t w;
        if (nk > MAXK) nk = MAXK;
        for (k = 0; k < nk; k++) {
            u32 first = (g + k) * runlen, last = first + runlen, at = src + (g + k) * pitch;
            if (last > N) last = N;
            rd_open(&in[k], G.hT, ARENA + KB(2 * k), KB(2), at, at + (last - first) * reclen);
            rd_bytes(&in[k], SLOTS + k * SLOT_SZ, reclen);
            active |= 1u << k;
        }
        wr_open(&w, G.hT, ARENA + KB(64), KB(16), dst + g * pitch);
        cnt = 0;
        while (active) {
            u32 best = MAXK;
            for (k = 0; k < nk; k++) {
                if (!(active & (1u << k))) continue;
                if (best == MAXK || cmprec(SLOTS + k * SLOT_SZ, SLOTS + best * SLOT_SZ, W, sec) < 0) best = k;
            }
            wr_bytes(&w, SLOTS + best * SLOT_SZ, reclen);
            if (rd_left(&in[best]) >= reclen) rd_bytes(&in[best], SLOTS + best * SLOT_SZ, reclen);
            else active &= ~(1u << best);
            if ((++cnt & 255) == 0) PUMP();
        }
        wr_close(&w);
    }
}

/* 4. nodes of one level, as FUN_40294f20 writes them: 16 bytes each, from the order and
 * the run lengths of this level, its child level and its parent level */
static u32 write_nodes(u32 Lthis, u32 Lchild, u32 Lparent, u32 depth)
{
    rd_t rt, rc, rp, ro;
    u8 *NODEBUF = ARENA + KB(32);
    u32 count = 0, child_idx = 0, parent_idx = 0, parent_acc = 0, parent_len = 0, songs;
    u8 *node;
    rd_open(&ro, G.hT, ARENA, KB(8), T(G.ORD), T(G.ORD) + 2 * N);
    rd_open(&rt, G.hT, ARENA + KB(8), KB(8), Lthis, Lthis + 2 * N + 2);
    if (Lchild) rd_open(&rc, G.hT, ARENA + KB(16), KB(8), Lchild, Lchild + 2 * N + 2);
    if (Lparent) { rd_open(&rp, G.hT, ARENA + KB(24), KB(8), Lparent, Lparent + 2 * N + 2); parent_len = rd_u16(&rp); }
    zero(NODEBUF, BIG);
    while ((songs = rd_u16(&rt)) != 0) {
        if (count && (count & 511) == 0) { lib_append(NODEBUF, BIG); zero(NODEBUF, BIG); }
        node = NODEBUF + (count & 511) * 16;
        node[0] = 1; node[1] = depth;
        wr16(node + 4, songs);
        wr16(node + 6, rd_u16(&ro));
        rd_skip(&ro, (songs - 1) * 2);
        wr16(node + 0xc, count ? count - 1 : 0xffff);
        wr16(node + 0xe, count + 1);
        if (!Lparent) wr16(node + 0xa, 0xffff);
        else {
            wr16(node + 0xa, parent_idx);
            parent_acc += songs;
            if (parent_len <= parent_acc) { parent_acc = 0; parent_len = rd_u16(&rp); parent_idx++; }
        }
        if (!Lchild) { wr16(node + 8, 0xffff); wr16(node + 2, 0); }
        else {
            u32 acc = 0, nch = 0;
            wr16(node + 8, child_idx);
            while (acc < songs) { acc += rd_u16(&rc); nch++; child_idx++; }
            wr16(node + 2, nch);
        }
        count++;
        if ((count & 255) == 0) PUMP();
    }
    if (count) {
        node = NODEBUF + ((count - 1) & 511) * 16;
        wr16(node + 0xe, 0xffff);
        lib_append(NODEBUF, (((count - 1) & 511) + 1) * 16);
    }
    return count;
}

/* 5. the per-song tables, in chunks of 4096 songs: inverse rank (also kept as the next
 * section's RANK), node index of the deepest level (also the album group), and for the
 * artist section the level-0 group (the artist group) */
static void tables(u32 sec, u32 levels)
{
    u32 base, inv_sec, nid_sec = 0;
    u16 *INV = (u16 *)(ARENA + KB(24)), *NID = (u16 *)(ARENA + KB(32)), *G0 = (u16 *)(ARENA + KB(40));
    inv_sec = V16(0x0a); V16(0x50 + 2 * sec) = inv_sec; V16(0x0a) += align(2 * N) >> 9;
    if (levels) { nid_sec = V16(0x0a); V16(0x5c + 2 * sec) = nid_sec; V16(0x0a) += align(2 * N) >> 9; }
    for (base = 0; base < N; base += CHUNK_N) {
        rd_t ro, rd, r0;
        u32 pos, n = N - base, grp = 0, left = 0, grp0 = 0, left0 = 0;
        if (n > CHUNK_N) n = CHUNK_N;
        zero((u8 *)INV, KB(24));
        rd_open(&ro, G.hT, ARENA, KB(8), T(G.ORD), T(G.ORD) + 2 * N);
        if (levels) rd_open(&rd, G.hT, ARENA + KB(8), KB(8), T(G.L[levels - 1]), T(G.L[levels - 1]) + 2 * N + 2);
        if (sec == 2) rd_open(&r0, G.hT, ARENA + KB(16), KB(8), T(G.L[0]), T(G.L[0]) + 2 * N + 2);
        for (pos = 0; pos < N; pos++) {
            u32 song = rd_u16(&ro);
            if (levels) { if (left == 0) { left = rd_u16(&rd); if (pos) grp++; } left--; }
            if (sec == 2) { if (left0 == 0) { left0 = rd_u16(&r0); if (pos) grp0++; } left0--; }
            if (song - base < CHUNK_N) { INV[song - base] = pos; NID[song - base] = grp; G0[song - base] = grp0; }
        }
        fs_write(G.hL, ((u32)inv_sec << 9) + 2 * base, INV, align(2 * n));
        fs_write(G.hT, T(G.RANK) + 2 * base, INV, align(2 * n));
        if (levels) fs_write(G.hL, ((u32)nid_sec << 9) + 2 * base, NID, align(2 * n));
        if (sec == 1) fs_write(G.hT, T(G.AGRP) + 2 * base, NID, align(2 * n));
        if (sec == 2) fs_write(G.hT, T(G.GGRP) + 2 * base, G0, align(2 * n));
        PUMP();
    }
}

static void pass(u32 sec)
{
    u32 W = sec == 0 ? 0x40 : sec == 1 ? 0x30 : 0x18;
    u32 reclen = W + 10, levels = sec == 0 ? 0 : sec;
    u32 cap = (KB(48) - 512) / reclen, pitch = align(cap * reclen);   /* records per run, bytes per run */
    u32 runlen, src, dst, t, pos, lvl, len[3] = { 0, 0, 0 };
    rd_t rs; wr_t wl[3], wo;
    u8 *prev = SLOTS, *cur = SLOTS + SLOT_SZ;

    extract(sec, W, reclen, cap, pitch);
    make_runs(sec, W, reclen, cap, pitch);
    runlen = cap; src = T(G.S1); dst = T(G.S0);
    while (runlen < N) {
        merge_pass(sec, W, reclen, src, dst, runlen, pitch);
        runlen *= MAXK; pitch *= MAXK;
        t = src; src = dst; dst = t;
    }

    /* one walk of the sorted stream: the sort order, and the run lengths of every level
     * (level 0 by name, level 1 by group1, level 2 by group2) */
    rd_open(&rs, G.hT, ARENA, KB(16), src, src + N * reclen);
    wr_open(&wo, G.hT, ARENA + KB(40), KB(8), T(G.ORD));
    for (lvl = 0; lvl < levels; lvl++) wr_open(&wl[lvl], G.hT, ARENA + KB(16 + 8 * lvl), KB(8), T(G.L[lvl]));
    for (pos = 0; pos < N; pos++) {
        rd_bytes(&rs, cur, reclen);
        wr_u16(&wo, rd16(cur + reclen - 2));
        if (pos) {
            u32 brk = 3;
            if (cmprec(prev, cur, W, 0) != 0) brk = 0;
            else if (levels > 1 && rd16(prev + W + 4) != rd16(cur + W + 4)) brk = 1;
            else if (levels > 2 && rd16(prev + W + 6) != rd16(cur + W + 6)) brk = 2;
            for (lvl = brk; lvl < levels; lvl++) { wr_u16(&wl[lvl], len[lvl]); len[lvl] = 0; }
        }
        for (lvl = 0; lvl < levels; lvl++) len[lvl]++;
        copy(prev, cur, reclen);
        if ((pos & 255) == 0) PUMP();
    }
    wr_close(&wo);
    for (lvl = 0; lvl < levels; lvl++) { wr_u16(&wl[lvl], len[lvl]); wr_u16(&wl[lvl], 0); wr_close(&wl[lvl]); }

    /* the nodes of every level, then the order, then the per-song tables */
    for (lvl = 0; lvl < levels; lvl++) {
        V16(0x8c + sec * 6 + lvl * 2) = V16(0x0a);
        V16(0x68 + sec * 6 + lvl * 2) = write_nodes(T(G.L[lvl]),
            lvl + 1 < levels ? T(G.L[lvl + 1]) : 0, lvl ? T(G.L[lvl - 1]) : 0, levels - lvl);
    }
    V16(0x44 + 2 * sec) = V16(0x0a);
    for (pos = 0; pos < 2 * N; pos += BIG) {
        u32 n = 2 * N - pos; if (n > BIG) n = BIG;
        fs_read(G.hT, T(G.ORD) + pos, ARENA, align(n));
        zero(ARENA + n, align(n) - n);
        lib_append(ARENA, n);
    }
    tables(sec, levels);
}
