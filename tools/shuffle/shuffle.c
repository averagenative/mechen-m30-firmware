/*
 * Replacement shuffle for the MECHEN M30 (Actions ATJ2127) music engine, mengine.ap
 * overlay bank 0x7b1a8000.  Built as MIPS16e and linked at 0x7b1a808c so that
 * shuffle_init() sits exactly where the stock init was; the stock picker at
 * 0x7b1a84b0 is patched with a branch to shuffle_next().
 *
 * Stock algorithm: 64 columns x N rows bitmap with the row count kept in a byte,
 * so libraries above 16383 tracks wrap and only the first ((N >> 6) & 0xff) * 64
 * tracks are reachable; plus a 3-try random pick that falls back to a linear scan.
 *
 * This version: keyed 4-round Feistel network over [0, 4^h) with cycle-walking.
 * Every track exactly once per cycle, any size up to 65535, 28 bytes of state,
 * new keys on every init.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef TEST
#define RAM(off)    (0x9fc15f00u + (off))
#define RAND_STUB   0xbfc11cc9u             /* li v1,0x0f ; break  (kernel module 0x0f) */
#else
#define RAM(off)    (TEST_RAM + (off))
#define RAND_STUB   TEST_RAND
#endif

#define RAND()      (((u32 (*)(u32, u32, u32, u32))RAND_STUB)(0, 0, 0, 0x35))

/* Globals shared with the rest of mengine: written exactly as the stock init did. */
#define CUR_INDEX   (*(volatile u16 *)RAM(0xb8))
#define TOTAL       (*(volatile u16 *)RAM(0x84))
#define OLD_FLAG    (*(volatile u8 *)RAM(0xb4))
#define OLD_COL     (*(volatile u8 *)RAM(0xb5))
#define OLD_ROW     (*(volatile u16 *)RAM(0xb6))
#define HIST_POS    (*(volatile u8 *)RAM(0xdc))
#define HIST_CNT    (*(volatile u8 *)RAM(0xe0))
#define HIST_BACK   (*(volatile u8 *)RAM(0xe1))
#define HIST_FE2    (*(volatile u8 *)RAM(0xe2))

/* Private state, reusing the stock 8 + 32 byte bitmaps at 0x9fc15f88. */
struct state {
    u32 key[4];
    u32 pos;        /* next domain index to try */
    u32 n;          /* track count the permutation was built for */
    u8 half;        /* domain is 4^half */
    u8 magic;
};
#define ST          ((volatile struct state *)RAM(0x88))
#define MAGIC       0xa5

static inline __attribute__((always_inline)) u32 mix(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

static inline __attribute__((always_inline)) void reseed(u32 total)
{
    volatile struct state *s = ST;
    u32 h = 1, k;
    int i;

    while ((1u << (2 * h)) < total)
        h++;
    k = s->key[3] ^ total;          /* whatever was in RAM adds a little entropy */
    for (i = 0; i < 4; i++) {
        k = mix(k + RAND() + 0x9e3779b9u * (u32)(i + 1));
        s->key[i] = k;
    }
    s->pos = 0;
    s->n = total;
    s->half = h;
    s->magic = MAGIC;
}

/* Replaces 0x7b1a808c: returns 1 if a shuffle cycle was set up for 'total' tracks. */
u32 shuffle_init(u16 total)
{
    CUR_INDEX = 0;
    if (total == 0)
        return 0;
    OLD_FLAG = 0;
    OLD_COL = 0;
    OLD_ROW = 0;
    HIST_POS = 0x10;
    HIST_CNT = 1;
    HIST_BACK = 1;
    HIST_FE2 = 0;
    TOTAL = total;
    reseed(total);
    return 1;
}

/* Replaces 0x7b1a84b0: next 1-based track index of this cycle, 0 when the cycle is done. */
u16 shuffle_next(void)
{
    volatile struct state *s = ST;
    u32 h, mask, end, n;

    if (s->magic != MAGIC || s->n == 0 || s->half == 0 || s->half > 8) {
        if (TOTAL == 0)
            return 0;
        reseed(TOTAL);
    }
    h = s->half;
    mask = (1u << h) - 1;
    end = 1u << (2 * h);
    n = s->n;
    while (s->pos < end) {
        u32 i = s->pos++;
        u32 l = i >> h, r = i & mask;
        int k;

        for (k = 0; k < 4; k++) {
            u32 t = l ^ (mix(r ^ s->key[k]) & mask);
            l = r;
            r = t;
        }
        i = (l << h) | r;
        if (i < n)
            return (u16)(i + 1);
    }
    return 0;
}
