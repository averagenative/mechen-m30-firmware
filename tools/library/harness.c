/* QEMU harness (MIPS32 o32, freestanding) for the MUSIC.LIB index passes of playlist.ap.
 *
 * It links the stock banks 0x400d4c00 (passes), 0x40114c00 (keys, tie refinement),
 * 0x40194c00 (header), 0x40294c00 (grouping, nodes), 0x48014400 (text) and 0x71812500 (sort)
 * at their real addresses, with their RAM and resident pointers moved by testbanks.py, and
 * gives them a RAM-backed MUSIC.LIB whose header and records come from a library the player
 * built.  Running the stock passes must then reproduce that library's index tables.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define RAMB 0x10000000u                      /* player 0x9fc00000 */
#define R8(o)  (*(volatile u8 *)(RAMB + (o)))
#define R16(o) (*(volatile u16 *)(RAMB + (o)))
#define R32(o) (*(volatile u32 *)(RAMB + (o)))
#define SECTOR 512

__attribute__((section(".testram"), used)) volatile u8 testram[0x40000];

extern u8 expect_image[];                      /* the library the player built */
extern u32 expect_len;
__attribute__((section(".disk"), used)) u8 disk[0x300000];   /* the file the passes write */

static u32 disk_pos;

/* ---- output ---- */
static long sys3(long n, long a, long b, long c)
{
    register long v0 asm("v0") = n, a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a3 asm("a3");
    asm volatile("syscall" : "+r"(v0), "=r"(a3) : "r"(a0), "r"(a1), "r"(a2) : "memory", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "hi", "lo");
    return v0;
}
static char obuf[1 << 16]; static int olen;
static void puts_(const char *s) { while (*s) obuf[olen++] = *s++; }
static void putu(u32 v) { char t[12]; int i = 0; do t[i++] = '0' + v % 10; while (v /= 10); while (i) obuf[olen++] = t[--i]; }
static void puthex(u32 v) { const char *h = "0123456789abcdef"; int i; puts_("0x"); for (i = 28; i >= 0; i -= 4) obuf[olen++] = h[(v >> i) & 15]; }
static void putb(u8 v) { const char *h = "0123456789abcdef"; obuf[olen++] = h[v >> 4]; obuf[olen++] = h[v & 15]; obuf[olen++] = ' '; }
static void flush(void) { sys3(4004, 1, (long)obuf, olen); olen = 0; }

/* ---- stubs the banks call (addresses from test.ld / testbanks.py) ---- */

/* module 0x10: 0 copy, 1 fill, 3 length */
__attribute__((section(".stub_libc"), used, noinline))
u32 stub_libc(u32 a, u32 b, u32 c, u32 fn)
{
    u8 *d = (u8 *)a, *s = (u8 *)b;
    u32 i;
    if (fn == 0) {                             /* FUN_40114c00 converts with destination 0: the copy
                                                  to address 0 is a stock quirk, harmless on the player */
        if (a >= 0x1000) for (i = 0; i < c; i++) d[i] = s[i];
        return a;
    }
    if (fn == 1) { for (i = 0; i < c; i++) d[i] = (u8)b; return a; }
    if (fn == 3) { for (i = 0; d[i]; i++) ; return i; }
    puts_("libc fn "); putu(fn); puts_(" unknown\n"); flush(); sys3(4001, 2, 0, 0);
    return 0;
}

/* module 0x11: the file system, with one open file (the library) and one position */
__attribute__((section(".stub_fs"), used, noinline))
u32 stub_fs(u32 h, u32 a, u32 b, u32 ctx, u32 id)
{
    u32 i;
    (void)h; (void)ctx;
    switch (id) {
    case 0: return 1;                          /* change directory */
    case 5: disk_pos = 0; return 1;            /* open by name: the library */
    case 7: return 1;                          /* close */
    case 0xb: disk_pos = a; return 1;          /* seek to a byte offset */
    case 0xd:                                  /* read b bytes to a */
        for (i = 0; i < b; i++) ((u8 *)a)[i] = disk_pos + i < sizeof disk ? disk[disk_pos + i] : 0;
        disk_pos += b;
        return b;
    case 0xe:                                  /* write b bytes from a */
        for (i = 0; i < b; i++) if (disk_pos + i < sizeof disk) disk[disk_pos + i] = ((u8 *)a)[i];
        disk_pos += b;
        return b;
    }
    puts_("fs op "); putu(id); puts_(" unknown\n"); flush(); sys3(4001, 2, 0, 0);
    return 0;
}

/* resident 0xbfc16411: append whole sectors at the current position, advance the counter */
__attribute__((section(".stub_append"), used, noinline))
u32 stub_append(u32 buf, u32 len)
{
    u32 n = (len - 1) / SECTOR + 1, i;
    for (i = 0; i < n * SECTOR; i++) if (disk_pos + i < sizeof disk) disk[disk_pos + i] = ((u8 *)buf)[i];
    disk_pos += n * SECTOR;
    R16(0x1540a) += n;
    return 1;
}

__attribute__((section(".stub_nop"), used, noinline))
u32 stub_nop(void) { return 1; }

/* module 6: 0x1a UTF-16LE to 8-bit in place, 0x1d UTF-8 to UTF-16LE */
__attribute__((section(".stub_text"), used, noinline))
u32 stub_text(u32 a, u32 b, u32 c, u32 fn)
{
    u8 *p = (u8 *)a;
    u32 i, n;
    if (fn == 0x1a) {
        /* Inferred from the sort order of a player-built library: Latin-1 characters become
         * their byte, in place, and the first character above U+00FF stops the conversion
         * without writing anything, so the rest of the buffer keeps the UTF-16 input.  This
         * reproduces the player's order for every name except a few whose bytes right after
         * such a character decide the order (see docs/library-builder.md). */
        for (i = 0; i + 1 < b; i += 2) {
            u32 ch = p[i] | (p[i + 1] << 8);
            if (ch >= 256) break;
            p[i / 2] = ch;
            if (!ch) break;
        }
        return 1;
    }
    if (fn == 0x1d) {                          /* a: UTF-8 in, b: UTF-16 out, c: &byte length */
        u8 *d = (u8 *)b;
        for (i = 0, n = 0; p[i]; ) {
            u32 ch = p[i], k = 1;
            if (ch >= 0xf0) { ch = ((ch & 7) << 18) | ((p[i + 1] & 0x3f) << 12) | ((p[i + 2] & 0x3f) << 6) | (p[i + 3] & 0x3f); k = 4; }
            else if (ch >= 0xe0) { ch = ((ch & 15) << 12) | ((p[i + 1] & 0x3f) << 6) | (p[i + 2] & 0x3f); k = 3; }
            else if (ch >= 0xc0) { ch = ((ch & 31) << 6) | (p[i + 1] & 0x3f); k = 2; }
            i += k;
            if (ch > 0xffff) ch = '?';
            d[n++] = ch; d[n++] = ch >> 8;
        }
        d[n] = d[n + 1] = 0;
        *(u16 *)c = n;
        return 1;
    }
    if (fn == 0x1b) return 1;
    puts_("text fn "); putu(fn); puts_(" unknown\n"); flush(); sys3(4001, 2, 0, 0);
    return 0;
}

/* ---- stock entry points (MIPS16: odd addresses) ---- */
static void (*const p_passes)(void) = (void (*)(void))0x400d5291;             /* FUN_400d5290 */
static u32 (*const p_header)(u32) = (u32 (*)(u32))0x4019502d;                  /* FUN_4019502c */
static void (*const p_key)(u32, u32, u32, u32) = (void (*)(u32, u32, u32, u32))0x48014695;  /* FUN_48014694 */

static u32 rd16(const u8 *p) { return p[0] | (p[1] << 8); }

void _start(void)
{
    u32 n, i, keylen, first_free, mism = 0, first_mism = 0;
    const u8 *hdr = expect_image;
    u32 fails = 0;

    n = rd16(hdr + 8);
    keylen = 0x10000 / n; if (keylen > 64) keylen = 64;
    first_free = 1 + (n * 256 + SECTOR - 1) / SECTOR;          /* sector after the records */
    puts_("library: "); putu(n); puts_(" songs, keylen "); putu(keylen); puts_(", tables from sector "); putu(first_free); puts_("\n");

    /* the disk: header and records as the player wrote them, index area blank */
    for (i = 0; i < sizeof disk; i++) disk[i] = i < first_free * SECTOR && i < expect_len ? expect_image[i] : 0;
    disk_pos = first_free * SECTOR;

    for (i = 0; i < sizeof testram; i++) testram[i] = 0;

    /* app state as FUN_40055068, FUN_40055108, the scan and FUN_40095160 leave it */
    R32(0x154d4) = RAMB + 0x22000; R32(0x154d0) = RAMB + 0x30000; R32(0x154d8) = RAMB + 0x29000;
    R32(0x154e8) = RAMB + 0x22000; R32(0x154ec) = RAMB + 0x24000; R32(0x154e0) = RAMB + 0x26000;
    R32(0x154e4) = RAMB + 0x2a000; R32(0x154f0) = RAMB + 0x28000; R32(0x154f4) = RAMB + 0x2c000;
    R32(0x154f8) = RAMB + 0x2e000; R32(0x154dc) = RAMB + 0x2e000;
    R32(0x15410) = 1;                         /* file system context */
    R32(0x1540c) = 1;                         /* file handle */
    R32(0x15408) = 0;                         /* text conversion handle */
    R16(0x1540a) = first_free;                /* sector counter */
    R16(0x15442) = 1;                         /* first record sector */
    R16(0x15440) = n;                         /* scan count */
    R8(0x158cc) = keylen;
    R8(0x158cf) = 0;
    for (i = 0; i < n; i++) {
        const u8 *rec = expect_image + 0x200 + i * 256;
        p_key(RAMB + 0x30000 + i * keylen, (u32)rec, 0, keylen);      /* title key, as the scan does */
        R8(0x29000 + i) = rec[0xf2];                                   /* track number byte */
    }

    p_passes();
    p_header(0);

    puts_("sector counter after the passes: "); putu(R16(0x1540a)); puts_("\n");
    for (i = 0; i < expect_len && i < sizeof disk; i++) {
        if (disk[i] != expect_image[i]) { if (!mism) first_mism = i; mism++; }
    }
    if (mism) {
        fails++;
        puts_("MISMATCH: "); putu(mism); puts_(" bytes differ, first at "); puthex(first_mism);
        puts_(" (sector "); putu(first_mism / SECTOR); puts_(")\n");
        puts_("  got      "); for (i = 0; i < 16; i++) putb(disk[first_mism + i]); puts_("\n");
        puts_("  expected "); for (i = 0; i < 16; i++) putb(expect_image[first_mism + i]); puts_("\n");
    } else {
        puts_("library reproduced byte for byte\n");
    }
    /* section header as the passes left it, for the record */
    puts_("counts:"); for (i = 0; i < 18; i++) { puts_(" "); putu(R16(0x15468 + i * 2)); } puts_("\n");
    puts_("nodes :"); for (i = 0; i < 18; i++) { puts_(" "); putu(R16(0x1548c + i * 2)); } puts_("\n");
    {   /* save the file the passes wrote, for diffing */
        long fd = sys3(4005, (long)"harness_out.lib", 0x301, 0644);   /* O_WRONLY|O_CREAT|O_TRUNC */
        if (fd >= 0) { sys3(4004, fd, (long)disk, R16(0x1540a) * SECTOR); sys3(4006, fd, 0, 0); }
    }
    puts_(fails ? "FAIL\n" : "ALL PASS\n");
    flush();
    sys3(4001, fails, 0, 0);
}
