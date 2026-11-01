/* QEMU harness: runs the real browser.ap dirlist bank (0x48714400, stock or patched)
 * with timed wheel events and checks list invariants and step counts. */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define PAGE 8
static volatile u32 now_ms;

__attribute__((section(".text.stubs"), used, noinline))
u32 test_timer(u32 a, u32 b, u32 c, u32 fn)
{
    return fn == 0x1c && !a && !b && !c ? now_ms : 0xdeadbeef;
}

__attribute__((section(".text.stubs2"), used, noinline))
u32 test_libc(u32 a, u32 b, u32 c, u32 fn)
{
    u8 *d = (u8 *)a, *s = (u8 *)b;
    u32 i;
    if (fn == 0) for (i = 0; i < c; i++) d[i] = s[i];
    else if (fn == 1) for (i = 0; i < c; i++) d[i] = (u8)b;
    return a;
}

__attribute__((section(".testram"), used)) volatile u8 testram[0x200];

static long sys3(long n, long a, long b, long c)
{
    register long v0 asm("v0") = n, a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a3 asm("a3");
    asm volatile("syscall" : "+r"(v0), "=r"(a3) : "r"(a0), "r"(a1), "r"(a2) : "memory", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "hi", "lo");
    return v0;
}
static char obuf[1 << 20]; static int olen;
static void puts_(const char *s) { while (*s) obuf[olen++] = *s++; }
static void putu(u32 v) { char t[12]; int i = 0; do t[i++] = '0' + v % 10; while (v /= 10); while (i) obuf[olen++] = t[--i]; }
static void puthex(u8 v) { const char *h = "0123456789abcdef"; obuf[olen++] = h[v >> 4]; obuf[olen++] = h[v & 15]; }

static u8 list[0x20];
static u8 cache[PAGE * 12];
static u8 names[PAGE][32];
static u32 rng = 12345;
static u32 rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

#define U16(o) (*(volatile u16 *)(list + (o)))
#define U8(o)  (*(volatile u8 *)(list + (o)))

static void (*volatile p_next)(u32, u8 *);
static void (*volatile p_prev)(u32, u8 *);

static void init_list(u32 total)
{
    u32 i;
    for (i = 0; i < sizeof(list); i++) list[i] = 0;
    U8(2) = PAGE;
    U16(8) = 1;
    U16(0xa) = total < PAGE ? total : PAGE;
    U16(0xc) = total;
    U16(0xe) = 1;
    U16(0x10) = 1;
    U16(0x12) = 1;
    for (i = 0; i < PAGE; i++) {
        *(u32 *)(cache + i * 12) = i;
        cache[i * 12 + 4] = 16;
        *(u32 *)(cache + i * 12 + 8) = (u32)names[i];
        names[i][0] = 'A' + i;
    }
}

/* model of the patched behaviour */
static u32 m_last, m_start, m_dir, m_count;
static u32 model_steps(u32 dir, u32 now)
{
    u32 held;
    if (dir != m_dir || now - m_last > 700) { m_start = now; m_count = 0; }
    m_dir = dir; m_last = now; m_count++;
    held = now - m_start;
    return held < 1000 && m_count < 6 ? 1 : held < 2500 && m_count < 16 ? 5 :
           held < 4000 && m_count < 26 ? 20 : held < 6000 && m_count < 40 ? 50 : 100;
}
static u32 m_taken;
static u32 model_move(u32 active, u32 total, u32 dir, u32 n)
{
    u32 k;
    for (k = 0; k < n; k++) {
        u32 before = active;
        if (dir == 1) {
            if (k && before >= total) break;
            active = active == total ? 1 : active + 1;
            if (active < before) { k++; break; }
        } else {
            if (k && before <= 1) break;
            active = active == 1 ? total : active - 1;
            if (active > before) { k++; break; }
        }
    }
    m_taken = k;
    return active;
}

void _start(void)
{
    static const u32 totals[] = { 1, 2, 7, 8, 9, 30, 386, 1000, 4000, 20000 };
    u32 t, ev, fails = 0, multi = 0, events = 0, h = 2166136261u;
    int patched = *(volatile u16 *)0x487144a8 != 0x64d3;   /* hook present? */
    extern u32 slow_flag;
    int slow_only = slow_flag;
    p_next = (void (*)(u32, u8 *))0x487144a9;
    p_prev = (void (*)(u32, u8 *))0x487146cd;
    puts_(patched ? "patched bank" : "stock bank"); puts_(slow_only ? ", slow events only\n" : ", mixed events\n");
    now_ms = 100000;
    for (t = 0; t < sizeof(totals) / sizeof(totals[0]); t++) {
        u32 total = totals[t], dir = 1, burst = 0;
        init_list(total);
        m_last = 0; m_start = 0; m_dir = 0; m_count = 0;
        for (ev = 0; ev < 3000; ev++) {
            u32 before, n, exp, a, top, bot, line, i;
            if (burst == 0) {               /* new gesture */
                burst = 1 + rnd() % 80;
                if (rnd() % 3 == 0) dir = 3 - dir;
                now_ms += 750 + rnd() % 1500;
            } else {
                now_ms += slow_only ? 750 + rnd() % 900 : 40 + rnd() % 400;
            }
            burst--;
            before = U16(0xe);
            n = model_steps(dir, now_ms);
            exp = model_move(before, total, dir, n);
            U8(0x14) = 0; U8(0x15) = 0;      /* the app clears these after redrawing */
            if (dir == 1) p_next((u32)cache, list); else p_prev((u32)cache, list);
            events++;
            a = U16(0xe); top = U16(8); bot = U16(0xa); line = U16(0x10);
            if (patched && a != exp) { fails++; puts_("MODEL MISMATCH total="); putu(total); puts_(" ev="); putu(ev); puts_(" exp="); putu(exp); puts_(" got="); putu(a); puts_("\n"); }
            if (!(1 <= top && top <= a && a <= bot && bot <= total && bot - top + 1 == (total < PAGE ? total : PAGE) && line == a - top + 1)) {
                fails++; puts_("INVARIANT total="); putu(total); puts_(" ev="); putu(ev); puts_(" top="); putu(top); puts_(" act="); putu(a);
                puts_(" bot="); putu(bot); puts_(" line="); putu(line); puts_("\n");
            }
            if (patched && m_taken > 1 && !((U8(0x14) == 4 || U8(0x14) == 5) && U8(0x15) == 2)) { fails++; puts_("FLAGS total="); putu(total); puts_(" dir="); putu(dir); puts_(" n="); putu(n); puts_(" before="); putu(before); puts_(" after="); putu(a); puts_(" mode="); putu(U8(0x14)); puts_(" upd="); putu(U8(0x15)); puts_("\n"); }
            if (n > 1) multi++;
            /* state dump for stock-vs-patched comparison */
            for (i = 0; i < 0x16; i++) h = (h ^ list[i]) * 16777619u;
            for (i = 0; i < sizeof(cache); i++) h = (h ^ cache[i]) * 16777619u;
            for (i = 0; i < 4; i++) puthex(h >> (24 - 8 * i));
            puts_("\n");
            if (fails > 20) goto out;
        }
    }
out:
    puts_("events "); putu(events); puts_(" multi-step "); putu(multi); puts_(" failures "); putu(fails); puts_("\n");
    sys3(4004, 1, (long)obuf, olen);
    sys3(4001, fails ? 1 : 0, 0, 0);
    for (;;) ;
}
