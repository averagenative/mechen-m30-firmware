/*
 * Whole-card shuffle, part D (mengine.ap shuffle bank 0x7b1a8000, linked at 0x7b1a8754 in the
 * bank's zero tail; patch_mengine.py raises the bank's load size to cover it).
 *
 * The folder order is a keyed Feistel permutation (part B), so the order already played can
 * be walked backward without storing it: folder_prev() steps the cycle position back to the
 * previous folder that was played.  folder_song() picks the song inside a folder from the
 * cycle key and position instead of a fresh random number, so stepping back returns to the
 * same song, and stepping forward again replays the same order.
 */
#include "playall.h"

/* Folder permutation state, same layout as part B. */
struct fstate {
    u16 key[4];
    u16 pos;        /* next domain index to try; the current folder is at pos - 1 */
    u16 n;
    u8 half;
    u8 magic;
};
#define FS          ((volatile struct fstate *)RAM(0xa4))
#define FMAGIC      0x5a

/*
 * Previous folder of this cycle, 1-based; 0 (state unchanged) at the start of the cycle.
 * Part B's folder_next() run from position j returns the folder at j and leaves the position
 * at j + 1 exactly when domain index j maps to a folder; otherwise it moves on, so try j - 1.
 */
u16 folder_prev(void)
{
    volatile struct fstate *s = FS;
    int save = s->pos, j;
    u16 t;

    if (s->magic != FMAGIC)
        return 0;
    for (j = save - 2; j >= 0; j--) {
        s->pos = (u16)j;
        t = ((u16 (*)(void))FOLDER_NEXT)();
        if (s->pos == j + 1)
            return t;
    }
    s->pos = (u16)save;
    return 0;
}

/* Song for the current folder visit, 1..c: fixed for a given cycle key and position. */
u16 folder_song(u16 c)
{
    volatile struct fstate *s = FS;

    return (u16)(((u32 (*)(u32))FOLDER_MIX)(s->pos ^ ((u32)s->key[3] << 16)) % c + 1);
}
