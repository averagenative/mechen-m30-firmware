/*
 * Whole-card shuffle, part B (mengine.ap shuffle bank, linked at 0x7b1a82ac right after
 * the track shuffle): a keyed Feistel permutation over the card's music folders, so every
 * folder is visited once per cycle in random order.  Small separate state from the track
 * shuffle, because entering a folder re-initialises the track shuffle.
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

/* 14 bytes in the stock shuffle's unused bitmap space, after the track-shuffle state. */
struct fstate {
    u16 key[4];
    u16 pos;        /* next domain index to try */
    u16 n;          /* folders in this cycle */
    u8 half;        /* domain is 4^half, half <= 7 */
    u8 magic;
};
#define FS          ((volatile struct fstate *)RAM(0xa4))
#define FMAGIC      0x5a
#define MAX_FOLDERS 16384u

static __attribute__((noinline)) u32 mix(u32 x)
{
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

/* Start a new cycle over n folders (n == 0 just invalidates the state). */
void folder_reseed(u16 n)
{
    volatile struct fstate *s = FS;
    u32 h = 1, k;
    int i;

    s->magic = 0;
    if (n == 0)
        return;
    if (n > MAX_FOLDERS)
        n = MAX_FOLDERS;
    while ((1u << (2 * h)) < n)
        h++;
    k = s->key[0] ^ ((u32)s->key[1] << 16) ^ n;
    for (i = 0; i < 4; i++) {
        k = mix(k + RAND() + 0x9e3779b9u * (u32)(i + 1));
        s->key[i] = (u16)(k ^ (k >> 16));
    }
    s->pos = 0;
    s->n = n;
    s->half = h;
    s->magic = FMAGIC;
}

/* Next folder of this cycle, 1-based; 0 when the cycle is done or not initialised. */
u16 folder_next(void)
{
    volatile struct fstate *s = FS;
    u32 h, mask, end, n;

    if (s->magic != FMAGIC || s->n == 0 || s->half == 0 || s->half > 7)
        return 0;
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
