/*
 * Whole-card shuffle, part C (mengine.ap shuffle bank, linked at 0x7b1a84b4 inside the stock
 * track picker, which is dead since its entry 0x7b1a84b0 branches to the new shuffle).
 *
 * Folder mode, Repeat All: the stock "next" walks to the next file, crossing folders in
 * depth-first order (0x7b0681e8), and with shuffle on only shuffles inside each folder.
 * playall_next() replaces that call in the Repeat All path: with shuffle on it jumps to
 * the next folder of a random folder order (part B) and plays a random song in it (part D
 * picks it from the order's key, so prev can return to it), so the whole card is covered;
 * with shuffle off it is exactly the stock call.
 */
#include "playall.h"

#define walk(t)         (((u16 (*)(u16))WALK)(t))

/* Replaces the Repeat All "next" call at 0x7b068088 (same signature as 0x7b0681e8). */
u32 playall_next(u32 *out)
{
    u16 t, c;

    if (SHUFFLE == 1) {
        t = ((u16 (*)(void))FOLDER_NEXT)();
        if (t == 0) {                       /* first use or cycle finished: recount */
            ((void (*)(u16))FOLDER_RESEED)(walk(0));
            t = ((u16 (*)(void))FOLDER_NEXT)();
        }
        if (t != 0 && walk(t) == t) {
            c = ((u16 (*)(u32, u32))COUNT_FILES)(0, 0);
            COUNT = c;
            POS = 0;
            if (c != 0)
                return ((u32 (*)(u32 *, u16))SEEK_FILE)(out, ((u16 (*)(u16))FOLDER_SONG)(c));
        }
        ((void (*)(u16))FOLDER_RESEED)(0);  /* card changed? recount next time */
        ((void (*)(void))RESET_ROOT)();
    }
    return ((u32 (*)(u32 *))NEXT_CROSS)(out);
}
