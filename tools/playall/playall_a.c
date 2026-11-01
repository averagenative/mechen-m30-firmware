/*
 * Whole-card shuffle, part A (mengine.ap hidden bank 0x7b068000, linked at 0x7b068484 over
 * the stock, never-called "count every file on the card" routine, 0x7b068484..0x7b06856b).
 * The stock function right after it (0x7b06856c, folder-mode "get location", called through
 * the engine's file-selector table) must stay intact, so this unit holds only the walker.
 */
#include "playall.h"

#define DIR_HEAD        0x82
#define DIR_NEXT        0x80
#define EXT_DIRS        1
#define MAX_LAYER       8

/*
 * Depth-first walk from the card root in the stock walker's order, counting folders that
 * contain songs.  target == 0: walk everything and return the count (ends at the root).
 * target > 0: stop inside the target-th song folder and return target (0 if not found).
 */
u16 walk(u16 target)
{
    u16 n = 0;

    ((void (*)(void))RESET_ROOT)();         /* also sets LAYER, POS, COUNT to 0 */
    for (;;) {
        if (VFS(DIR_HEAD, AUDIO_EXT, 1) && ++n == target)
            return n;
        if (LAYER < MAX_LAYER && VFS(DIR_HEAD, EXT_DIRS, 1)) {
            if (!CD('/'))
                return 0;
            LAYER++;
            continue;
        }
        for (;;) {
            if (LAYER == 0)
                return target ? 0 : n;
            if (!CD('|'))
                return 0;
            LAYER--;
            if (VFS(DIR_NEXT, EXT_DIRS, 1)) {
                if (!CD('/'))
                    return 0;
                LAYER++;
                break;
            }
        }
    }
}
