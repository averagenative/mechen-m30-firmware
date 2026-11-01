/*
 * QEMU harness for the screen-off media-key patch.  Runs the real common key filter
 * (bank 0x48694600, FUN_48694658) from one app, stock and patched, over every key, press
 * type, screen state and wake-key state, and checks that the two agree except where the
 * patch should change behavior: with the screen off, play/pause, prev, next, volume up and
 * volume down events that the stock filter turns into a wake (message 0x106, key discarded)
 * must instead pass to the app (return 1), send no message, leave the screen state alone,
 * and reset the idle counters on key down exactly as the screen-on path does.
 * Banks come from patch_keys.py --testbanks, with the literal pool moved to this test RAM.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#define SYS   ((volatile u8 *)0x10000000)     /* system state: +1 power-key flag, +3 wake key, +0x18 screen */
#define APPP  (*(volatile u32 *)0x10000100)   /* pointer to app info; app id at +1 */
#define APP   ((volatile u8 *)0x10000140)
#define CNT   ((volatile u16 *)0x10000180)    /* idle counters */
#define LAST  (*(volatile u8 *)0x10000200)    /* last key */
#define HOOK  ((u32 (*)(u32 *))0x48694659)    /* FUN_48694658, MIPS16 */

__attribute__((section(".testram"), used)) volatile u8 testram[0x300];

static volatile u32 sent0;
static volatile int nsent;

__attribute__((section(".text.stubs"), used, noinline))
void stub_send(u32 app, u32 *msg)
{
    (void)app;
    if (nsent++ == 0)
        sent0 = msg[0];
}

extern u8 bank_run[0x600];
extern const u8 bank_stock[0x600], bank_patched[0x600];

struct res { u32 r, m0; int n; u16 cnt[8]; u8 last, wake, screen; };

static long sys3(long n, long a, long b, long c)
{
    register long v0 asm("v0") = n, a0 asm("a0") = a, a1 asm("a1") = b, a2 asm("a2") = c, a3 asm("a3");
    asm volatile("syscall" : "+r"(v0), "=r"(a3) : "r"(a0), "r"(a1), "r"(a2) : "memory", "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7", "t8", "t9", "hi", "lo");
    return v0;
}
static char obuf[1 << 16]; static int olen;
static void puts_(const char *s) { while (*s) obuf[olen++] = *s++; }
static void puthex(u32 v, int digits) { const char *h = "0123456789abcdef"; while (digits--) obuf[olen++] = h[(v >> (4 * digits)) & 15]; }
static void putu(u32 v) { char t[12]; int i = 0; do t[i++] = '0' + v % 10; while (v /= 10); while (i) obuf[olen++] = t[--i]; }

static void run(const u8 *img, u8 screen, u8 key, u16 type, u8 wake, struct res *o)
{
    u32 msg[2] = { 0, 0 };
    int i;
    for (i = 0; i < 0x600; i++) bank_run[i] = img[i];
    for (i = 0; i < 0x40; i++) SYS[i] = 0;
    SYS[1] = 1; SYS[3] = wake; SYS[0x18] = screen;
    APPP = 0x10000140; APP[1] = 7;
    for (i = 0; i < 8; i++) CNT[i] = 0x1111 * (i + 1);
    LAST = 0xee; nsent = 0; sent0 = 0;
    ((u8 *)msg)[2] = key; ((u16 *)msg)[2] = type;
    o->r = HOOK(msg);
    o->n = nsent; o->m0 = sent0;
    for (i = 0; i < 8; i++) o->cnt[i] = CNT[i];
    o->last = LAST; o->wake = SYS[3]; o->screen = SYS[0x18];
}

static int same(const struct res *a, const struct res *b)
{
    int i;
    if (a->r != b->r || a->n != b->n || a->m0 != b->m0 || a->last != b->last || a->wake != b->wake || a->screen != b->screen)
        return 0;
    for (i = 0; i < 8; i++) if (a->cnt[i] != b->cnt[i]) return 0;
    return 1;
}

static void show(const char *tag, const struct res *x)
{
    puts_(tag); puts_(" r="); putu(x->r); puts_(" msgs="); putu(x->n); puts_(" m0="); puthex(x->m0, 3);
    puts_(" cnt0="); puthex(x->cnt[0], 4); puts_(" last="); puthex(x->last, 2); puts_(" wake="); puthex(x->wake, 2);
    puts_(" screen="); putu(x->screen); puts_("\n");
}

void _start(void)
{
    static const u8 keys[] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x7f, 0x80, 0x81 };
    static const u16 types[] = { 0x2000, 0x1000, 0x0800, 0x0400 };
    static const u8 screens[] = { 0, 2 };
    u32 cases = 0, changed = 0, fails = 0, si, ki, ti, wi, i;
    struct res s, p, e;

    for (si = 0; si < 2; si++)
    for (ki = 0; ki < sizeof(keys); ki++)
    for (ti = 0; ti < 4; ti++)
    for (wi = 0; wi < 4; wi++) {
        u8 k = keys[ki], scr = screens[si];
        u8 wakes[4] = { 0, k, 0x07, 0x0a };
        int media = k == 1 || k == 2 || k == 3 || k == 5 || k == 6;
        run(bank_stock, scr, k, types[ti], wakes[wi], &s);
        run(bank_patched, scr, k, types[ti], wakes[wi], &p);
        cases++;
        e = s;
        if (media && scr && s.n == 1 && s.m0 == 0x106 && s.r == 0) {
            changed++;
            e.r = 1; e.n = 0; e.m0 = 0;
            for (i = 0; i < 8; i++) e.cnt[i] = 0x1111 * (i + 1);
            if (types[ti] == 0x2000) e.cnt[0] = e.cnt[1] = e.cnt[2] = e.cnt[4] = 0;
        }
        if (media && scr && !(s.n == 1 && s.m0 == 0x106) && !(s.r == 0 && s.n == 0)) {
            fails++; puts_("UNEXPECTED STOCK RESULT key="); puthex(k, 2); puts_("\n");
        }
        if (!same(&e, &p)) {
            fails++;
            puts_("FAIL screen="); putu(scr); puts_(" key="); puthex(k, 2); puts_(" type="); puthex(types[ti], 4);
            puts_(" wake="); puthex(wakes[wi], 2); puts_("\n");
            show("  stock   ", &s); show("  expect  ", &e); show("  patched ", &p);
        }
        if (scr && k == 3 && types[ti] == 0x2000 && wi == 0) {
            puts_("screen off, next key down:\n"); show("  stock   ", &s); show("  patched ", &p);
        }
        if (scr && k == 0x0a && types[ti] == 0x2000 && wi == 0) {
            puts_("screen off, wheel down:\n"); show("  stock   ", &s); show("  patched ", &p);
        }
    }
    if (changed != 5 * 4 * 3) { fails++; puts_("CHANGED-CASE COUNT FAIL\n"); }
    puts_("cases "); putu(cases); puts_(", changed by the patch "); putu(changed);
    puts_(fails ? "\nFAILURES: " : "\nALL PASS, failures: "); putu(fails); puts_("\n");
    sys3(4004, 1, (long)obuf, olen);
    sys3(4001, fails ? 1 : 0, 0, 0);
    for (;;) ;
}
