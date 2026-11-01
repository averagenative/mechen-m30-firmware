/*
 * Scroll-wheel acceleration for the MECHEN M30 (Actions ATJ2127) folder view.
 *
 * The shared dirlist control (bank 0x48714400 in browser.ap) moves the selection
 * one item per wheel detent via FUN_487144a8 (next) / FUN_487146cc (prev).  Their
 * first instruction is patched into a branch to accel_next / accel_prev here.
 * These time the detents with the kernel millisecond clock and, while the wheel
 * keeps turning, call the untouched stock one-step code several times per detent.
 * A multi-step move never wraps past either end of the list and forces a full
 * page reload (redraw mode 4, update 2), which the stock reloader handles for any
 * position.  A single step is exactly the stock code path.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef TEST
#ifndef TIMER_STUB
#define TIMER_STUB  0xbfc16a25u     /* browser.ap resident stub: li v1,0x0f ; break */
#endif
#define ACC_RAM     0x9fc15bf4u     /* 12 bytes no firmware file references */
#else
#define TIMER_STUB  TEST_TIMER
#define ACC_RAM     TEST_RAM
#endif

#define NOW_MS()    (((u32 (*)(u32, u32, u32, u32))TIMER_STUB)(0, 0, 0, 0x1c))

/*
 * v2: v1 (400 ms gap, time-only ramp) never sped up on the player, so the gap is wider
 * and the ramp also counts detents, in case the player handles each detent slowly.
 * v3: two more gears (50, 100) for lists of thousands of files.
 */
#define GAP_MS      700     /* longer pause between detents ends a burst */
#define FAST1_MS    1000    /* continuous turning before 5 items per detent ... */
#define FAST1_N     6       /* ... or this many detents in the burst */
#define FAST2_MS    2500    /* ... before 20 items per detent */
#define FAST2_N     16
#define FAST3_MS    4000    /* ... before 50 */
#define FAST3_N     26
#define FAST4_MS    6000    /* ... before 100 */
#define FAST4_N     40

struct accel {
    u32 last;               /* time of the previous detent */
    u32 start;              /* time the current burst started */
    u16 dir;                /* direction of the current burst */
    u16 count;              /* detents in the current burst */
};
#define ACC         ((volatile struct accel *)ACC_RAM)

/* dirlist control fields */
#define ACTIVE(l)   (*(volatile u16 *)((l) + 0x0e))
#define TOTAL(l)    (*(volatile u16 *)((l) + 0x0c))
#define MODE(l)     (*(volatile u8 *)((l) + 0x14))
#define UPDATE(l)   (*(volatile u8 *)((l) + 0x15))

/* trampolines (tramp.S): replayed first instruction + branch into the stock body */
extern void stock_next(u32 ctx, u8 *list);
extern void stock_prev(u32 ctx, u8 *list);

static __attribute__((noinline)) u32 steps_for(u32 dir)
{
    volatile struct accel *a = ACC;
    u32 now = NOW_MS(), held, n;

    if (dir != a->dir || now - a->last > GAP_MS) {
        a->start = now;
        a->count = 0;
    }
    a->dir = dir;
    a->last = now;
    n = a->count;
    if (n < 0xffff)
        a->count = ++n;
    held = now - a->start;
    if (held < FAST1_MS && n < FAST1_N)
        return 1;
    if (held < FAST2_MS && n < FAST2_N)
        return 5;
    if (held < FAST3_MS && n < FAST3_N)
        return 20;
    if (held < FAST4_MS && n < FAST4_N)
        return 50;
    return 100;
}

/* replaces FUN_487144a8 */
void accel_next(u32 ctx, u8 *list)
{
    u32 n = steps_for(1), k;

    for (k = 0; k < n; k++) {
        u16 before = ACTIVE(list);

        if (k && before >= TOTAL(list))
            break;                  /* at the end: only a single step may wrap */
        stock_next(ctx, list);
        if (ACTIVE(list) < before) {
            k++;
            break;                  /* wrapped to the top */
        }
    }
    if (k > 1) {
        MODE(list) = 4;             /* reload the whole visible page */
        UPDATE(list) = 2;
    }
}

/* replaces FUN_487146cc */
void accel_prev(u32 ctx, u8 *list)
{
    u32 n = steps_for(2), k;

    for (k = 0; k < n; k++) {
        u16 before = ACTIVE(list);

        if (k && before <= 1)
            break;                  /* at the top: only a single step may wrap */
        stock_prev(ctx, list);
        if (ACTIVE(list) > before) {
            k++;
            break;                  /* wrapped to the bottom */
        }
    }
    if (k > 1) {
        MODE(list) = 4;
        UPDATE(list) = 2;
    }
}
