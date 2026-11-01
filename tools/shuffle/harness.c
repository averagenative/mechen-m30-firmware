/* QEMU harness (MIPS32 o32, freestanding) for the MIPS16e shuffle blob linked at 0x7b1a808c. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

extern u32 shuffle_init(u16 total);
extern u16 shuffle_next(void);
/* blob lives in another 256 MB region: call through pointers (jalr), MIPS16 bit set by the linker */
static u32 (*volatile p_init)(u16);
static u16 (*volatile p_next)(void);
__attribute__((section(".testram"), used)) volatile u8 testram[0x200];

#define RAMB 0x10000000u
#define R8(o)  (*(volatile u8 *)(RAMB + (o)))
#define R16(o) (*(volatile u16 *)(RAMB + (o)))

static u32 seed = 0x12345678;
static u32 ncalls;

__attribute__((section(".text.rand"), used, noinline))
u32 test_rand(u32 a, u32 b, u32 c, u32 fn)
{
    ncalls++;
    if (a || b || c || fn != 0x35)
        return 0xdeadbeef;
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
    return seed;
}

static long sys3(long n, long a, long b, long c)
{
    register long v0 asm("v0") = n, a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a3 asm("a3");
    asm volatile("syscall" : "+r"(v0), "=r"(a3) : "r"(a0), "r"(a1), "r"(a2) : "memory", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "hi", "lo");
    return v0;
}
static char obuf[8192]; static int olen;
static void puts_(const char *s) { while (*s) obuf[olen++] = *s++; }
static void putu(u32 v) { char t[12]; int i = 0; do t[i++] = '0' + v % 10; while (v /= 10); while (i) obuf[olen++] = t[--i]; }

static u8 seen[65536 / 8];

static int run_cycle(u32 n, u32 *first10pct, u16 *firsts)
{
    u32 i, got = 0;
    for (i = 0; i < sizeof(seen); i++) seen[i] = 0;
    *first10pct = 0;
    for (;;) {
        u16 v = p_next();
        if (v == 0) break;
        if (v > n) return -1;                               /* out of range */
        if (seen[(v - 1) >> 3] & (1 << ((v - 1) & 7))) return -2; /* repeat */
        seen[(v - 1) >> 3] |= 1 << ((v - 1) & 7);
        if (got < 100 && v <= n / 10) (*first10pct)++;
        if (got < 6) firsts[got] = v;
        got++;
    }
    return got == n ? 0 : -3;                               /* -3: some track never played */
}

void _start(void)
{
    p_init = (u32 (*)(u16))((u32)shuffle_init | 1);     /* MIPS16 entry: ISA bit set */
    p_next = (u16 (*)(void))((u32)shuffle_next | 1);
    static const u32 sizes[] = { 1, 2, 3, 5, 64, 65, 100, 1000, 4095, 4096, 4097, 16383, 16384,
                                 16447, 17954, 18243, 20000, 21000, 30000, 45000, 65535 };
    u32 k, fails = 0;
    for (k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        u32 n = sizes[k], f10, c;
        u16 firsts[6] = {0};
        u32 r = p_init((u16)n);
        int ok = r == 1 && R16(0x84) == n && R16(0xb8) == 0 && R8(0xdc) == 0x10 && R8(0xe0) == 1 && R8(0xe1) == 1;
        int res = run_cycle(n, &f10, firsts);
        /* second cycle after re-init must differ in order */
        u16 again[6] = {0}; u32 f10b;
        p_init((u16)n); run_cycle(n, &f10b, again);
        int differs = n < 8 || again[0] != firsts[0] || again[1] != firsts[1] || again[2] != firsts[2];
        if (!ok || res != 0 || !differs) fails++;
        puts_("n="); putu(n); puts_(ok ? " globals-ok" : " GLOBALS-BAD");
        puts_(res == 0 ? " full-cycle-ok" : res == -1 ? " OUT-OF-RANGE" : res == -2 ? " REPEAT" : " MISSING");
        puts_(differs ? " reseeds" : " SAME-ORDER");
        if (n >= 1000) { puts_(" first100-in-first10%="); putu(f10); }
        puts_(" first:"); for (c = 0; c < 6 && c < n; c++) { puts_(" "); putu(firsts[c]); }
        puts_("\n");
    }
    /* stale/garbage state (e.g. RAM never initialised): next() must self-seed from TOTAL */
    R8(0x88 + 25) = 0; R16(0x84) = 21000;
    { u32 f10; u16 fs[6]; int res = run_cycle(21000, &f10, fs);
      puts_(res == 0 ? "stale-state self-reseed ok\n" : "STALE-STATE FAIL\n"); if (res) fails++; }
    /* total==0 */
    if (p_init(0) != 0 || R16(0xb8) != 0) { fails++; puts_("ZERO-TOTAL FAIL\n"); }
    puts_("rand calls: "); putu(ncalls); puts_(fails ? "\nFAILURES: " : "\nALL PASS, failures: "); putu(fails); puts_("\n");
    sys3(4004, 1, (long)obuf, olen);
    sys3(4001, fails, 0, 0);
    for (;;) ;
}
