/* QEMU harness (MIPS32 o32, freestanding) for the whole-card shuffle units A (0x7b068484)
 * and B (0x7b1a82ac), with a mock file system and mock stock engine routines. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

extern u32 playall_next(u32 *out);

#define RAMB 0x10000000u
#define R8(o)  (*(volatile u8 *)(RAMB + (o)))
#define R16(o) (*(volatile u16 *)(RAMB + (o)))
#define R32(o) (*(volatile u32 *)(RAMB + (o)))
#define AUDIO_EXT_VAL 0x00c0ffeeu

__attribute__((section(".testram"), used)) volatile u8 testram[0x200];

/* ---- mock tree ---- */
#define MAXN 700
static short parent[MAXN], first_child[MAXN], next_sib[MAXN], nfiles[MAXN], depth[MAXN];
static int nn;
static int cur, dir_cur[MAXN], file_cur[MAXN];
static u32 seed = 0x2468ace1;
static u32 rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static int add(int p, int files)
{
    int n = nn++, c;
    parent[n] = p; first_child[n] = -1; next_sib[n] = -1; nfiles[n] = files;
    depth[n] = p < 0 ? 0 : depth[p] + 1;
    if (p >= 0) {
        if (first_child[p] < 0) first_child[p] = n;
        else { for (c = first_child[p]; next_sib[c] >= 0; c = next_sib[c]) ; next_sib[c] = n; }
    }
    return n;
}

static void build(int variant)
{
    int a, b, i, j, k, d;
    nn = 0;
    add(-1, 0);                                     /* root: no songs */
    for (i = 0; i < 4; i++) {                       /* Albums, Mixes, Random, ... */
        a = add(0, i == 2 ? 30 : 0);                /* "Random" has songs directly */
        for (j = 0; j < 12 + (int)(rnd() % 10); j++) {    /* artists */
            b = add(a, rnd() % 4 == 0 ? 2 : 0);
            for (k = 0; k < 1 + (int)(rnd() % 5); k++)   /* albums */
                add(b, rnd() % 7 == 0 ? 0 : 1 + rnd() % 18);
        }
    }
    a = add(0, 0);                                  /* deep chain: 10 levels, songs at each */
    for (d = 0; d < 10; d++) a = add(a, 3);
    if (variant == 1)                               /* "card changed": drop half the songs */
        for (i = 0; i < nn; i++) if (i % 2) nfiles[i] = 0;
    for (i = 0; i < nn; i++) { dir_cur[i] = -1; file_cur[i] = 0; }
    cur = 0;
}

/* expected order of song folders, mirroring the stock walker (enter children while layer < 8) */
static short order[MAXN]; static int norder;
static void dfs(int n)
{
    int c;
    if (nfiles[n] > 0) order[norder++] = n;
    if (depth[n] < 8)
        for (c = first_child[n]; c >= 0; c = next_sib[c]) dfs(c);
}

/* ---- mock stock routines at fixed addresses ---- */
static u32 vfs_calls, seeks, fallbacks, errors, last_seek_dir, last_seek_no, rand_calls;

__attribute__((section(".stub_rand"), used, noinline))
u32 stub_rand(u32 a, u32 b, u32 c, u32 fn)
{
    rand_calls++;
    if (a || b || c || fn != 0x35) errors++;
    return rnd();
}

__attribute__((section(".stub_vfs"), used, noinline))
u32 stub_vfs(u32 h, u32 mode, u32 buf, u32 ext, u32 fid)
{
    vfs_calls++;
    if (h != 0x1234 || buf != 0) errors++;
    if (fid == 0) {                                 /* cd */
        if (mode == ':') { cur = 0; dir_cur[0] = -1; return 1; }
        if (mode == '/') {
            if (dir_cur[cur] < 0) { errors++; return 0; }
            cur = dir_cur[cur]; dir_cur[cur] = -1; file_cur[cur] = 0; return 1;
        }
        if (mode == '|') {
            if (cur == 0) return 0;
            dir_cur[parent[cur]] = cur; cur = parent[cur]; return 1;
        }
        errors++; return 0;
    }
    if (fid != 1) { errors++; return 0; }
    if (ext == 1) {                                 /* directories */
        if (mode == 0x82) dir_cur[cur] = first_child[cur];
        else if (mode == 0x80) { if (dir_cur[cur] < 0) return 0; dir_cur[cur] = next_sib[dir_cur[cur]]; }
        else { errors++; return 0; }
        return dir_cur[cur] >= 0 ? (u32)dir_cur[cur] + 1 : 0;
    }
    if (ext != AUDIO_EXT_VAL) { errors++; return 0; }
    if (mode == 0x82) file_cur[cur] = nfiles[cur] > 0 ? 1 : 0;
    else if (mode == 0x80) file_cur[cur] = file_cur[cur] < nfiles[cur] ? file_cur[cur] + 1 : 0;
    return file_cur[cur];
}

__attribute__((section(".stub_reset"), used, noinline))
void stub_reset_root(void)
{
    R8(0x22) = 0; R16(0x24) = 0; R16(0x26) = 0;
    stub_vfs(R32(0x34), ':', 0, 0, 0);
}

__attribute__((section(".stub_count"), used, noinline))
u16 stub_count_files(u32 a, u32 b)
{
    if (a || b) errors++;
    return (u16)nfiles[cur];
}

__attribute__((section(".stub_seek"), used, noinline))
u32 stub_seek_file(u32 *out, u16 no)
{
    seeks++;
    if (no < 1 || no > (u16)nfiles[cur] || R16(0x26) != (u16)nfiles[cur] || R16(0x24) != 0) errors++;
    last_seek_dir = cur; last_seek_no = no;
    if (out) *out = cur * 1000 + no;
    return 1;
}

__attribute__((section(".stub_nextx"), used, noinline))
u32 stub_next_cross(u32 *out)
{
    fallbacks++;
    if (out) *out = 0xfa11;
    return 1;
}

/* ---- output ---- */
static long sys3(long n, long a, long b, long c)
{
    register long v0 asm("v0") = n, a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a3 asm("a3");
    asm volatile("syscall" : "+r"(v0), "=r"(a3) : "r"(a0), "r"(a1), "r"(a2) : "memory", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "hi", "lo");
    return v0;
}
static char obuf[4096]; static int olen;
static void puts_(const char *s) { while (*s) obuf[olen++] = *s++; }
static void putu(u32 v) { char t[12]; int i = 0; do t[i++] = '0' + v % 10; while (v /= 10); while (i) obuf[olen++] = t[--i]; }

static u8 seen[MAXN];

void _start(void)
{
    u32 (*volatile next)(u32 *) = (u32 (*)(u32 *))((u32)playall_next | 1);
    u32 fails = 0, out, i, cyc, k;

    build(0);
    norder = 0; dfs(0);
    R32(0x34) = 0x1234; R32(0x3c) = AUDIO_EXT_VAL; R8(0x23) = 1;

    /* 1: three full cycles, every song folder exactly once per cycle, depth var consistent */
    for (cyc = 0; cyc < 3; cyc++) {
        u32 bad = 0, firsts[4];
        for (i = 0; i < MAXN; i++) seen[i] = 0;
        for (k = 0; k < (u32)norder; k++) {
            if (next(&out) != 1) bad++;
            if (seen[last_seek_dir]++) bad++;
            if (R8(0x22) != (u8)depth[last_seek_dir] || out != last_seek_dir * 1000 + last_seek_no) bad++;
            if (k < 4) firsts[k] = last_seek_dir;
        }
        for (k = 0; k < (u32)norder; k++) if (!seen[order[k]]) bad++;
        puts_("cycle "); putu(cyc); puts_(": folders="); putu(norder); puts_(bad ? " FAIL" : " ok (each once, depth ok)");
        puts_(" first:"); for (k = 0; k < 4; k++) { puts_(" "); putu(firsts[k]); } puts_("\n");
        if (bad) fails++;
    }
    puts_("deep chain folders reachable only to layer 8: ");
    { u32 deepest = 0; for (k = 0; k < (u32)norder; k++) if ((u32)depth[order[k]] > deepest) deepest = depth[order[k]];
      putu(deepest); puts_(deepest == 8 ? " ok\n" : " FAIL\n"); if (deepest != 8) fails++; }
    if (fallbacks) { fails++; puts_("unexpected fallbacks\n"); }

    /* 2: shuffle off = stock call only */
    R8(0x23) = 0; k = fallbacks; i = seeks;
    next(&out);
    if (fallbacks != k + 1 || seeks != i || out != 0xfa11) { fails++; puts_("shuffle-off FAIL\n"); } else puts_("shuffle off -> stock next ok\n");
    R8(0x23) = 1;

    /* 3: card changes mid-cycle (fewer song folders): must recover and cover the new set */
    for (k = 0; k < 10; k++) next(&out);
    build(1); norder = 0; dfs(0);
    { u32 bad = 0, fb0 = fallbacks, calls = 0;
      for (i = 0; i < MAXN; i++) seen[i] = 0;
      for (k = 0; k < 4u * norder && calls < 4000; k++) {
          u32 s0 = seeks;
          next(&out); calls++;
          if (seeks != s0) { if (nfiles[last_seek_dir] == 0) bad++; seen[last_seek_dir] = 1; }
      }
      for (k = 0; k < (u32)norder; k++) if (!seen[order[k]]) bad++;
      puts_("after card change: folders="); putu(norder); puts_(" fallbacks="); putu(fallbacks - fb0);
      puts_(bad ? " FAIL\n" : " ok (recovered, all new folders reached)\n"); if (bad) fails++; }

    /* 4: card with no songs at all -> stock call */
    { int n; for (n = 0; n < nn; n++) nfiles[n] = 0; }
    k = fallbacks; next(&out);
    if (fallbacks != k + 1) { fails++; puts_("no-songs FAIL\n"); } else puts_("no songs on card -> stock next ok\n");

    puts_("vfs calls: "); putu(vfs_calls); puts_("  rand calls: "); putu(rand_calls); puts_("  stub errors: "); putu(errors);
    if (errors) fails++;
    puts_(fails ? "\nFAILURES: " : "\nALL PASS, failures: "); putu(fails); puts_("\n");
    sys3(4004, 1, (long)obuf, olen);
    sys3(4001, fails, 0, 0);
    for (;;) ;
}
