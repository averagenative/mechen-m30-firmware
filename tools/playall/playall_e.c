/*
 * Whole-card shuffle, part E (mengine.ap bank 0x7b0a8000, linked at 0x7b0a8458 in the bank's
 * zero tail; patch_mengine.py raises the bank's load size to cover it).
 *
 * Folder mode, Repeat All: the stock "previous" walks back to the previous file, crossing
 * folders (0x7b0a82ac), so with whole-card shuffle on, prev stays in the current folder while
 * next jumps across the card.  playall_prev() replaces both Repeat All calls to it: with
 * shuffle on it steps the folder order back (part D) and replays the song played there; at
 * the start of a cycle, or with shuffle off, it is exactly the stock call.
 */
#include "playall.h"

#define walk(t)         (((u16 (*)(u16))WALK)(t))

/* Replaces the Repeat All "prev" calls at 0x7b0a80de and 0x7b0a812e (same signature). */
u32 playall_prev(u32 *out)
{
    u16 t, c;

    if (SHUFFLE == 1 && (t = ((u16 (*)(void))FOLDER_PREV)()) != 0) {
        if (walk(t) == t) {
            c = ((u16 (*)(u32, u32))COUNT_FILES)(0, 0);
            COUNT = c;
            POS = 0;
            if (c != 0)
                return ((u32 (*)(u32 *, u16))SEEK_FILE)(out, ((u16 (*)(u16))FOLDER_SONG)(c));
        }
        ((void (*)(u16))FOLDER_RESEED)(0);  /* card changed? recount next time */
        ((void (*)(void))RESET_ROOT)();
    }
    return ((u32 (*)(u32 *))PREV_CROSS)(out);
}
