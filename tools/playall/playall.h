/*
 * Whole-card shuffle: addresses and engine state shared by parts A (walker, mengine bank 1),
 * C (playall_next, mengine shuffle bank 6), D (folder_prev and folder_song, bank 6), and
 * E (playall_prev, bank 2).  Part B is the folder permutation.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;

#ifndef TEST
#define RAM(off)        (0x9fc15f00u + (off))
#define VFS_STUB        0xbfc11ccdu     /* li v1,0x11 ; break  (file system module) */
#define RAND_STUB       0xbfc11cc9u     /* li v1,0x0f ; break  (kernel module) */
#define RESET_ROOT      0x7a052f35u     /* stock: reset folder state, cd ':' */
#define COUNT_FILES     0x7b068391u     /* stock: count songs in current folder */
#define SEEK_FILE       0x7b0e81f9u     /* stock: select song #n of current folder */
#define NEXT_CROSS      0x7b0681e9u     /* stock: next song, crossing folders */
#define WALK            0x7b068485u     /* part A */
#define FOLDER_RESEED   0x7b1a82d5u     /* part B */
#define FOLDER_NEXT     0x7b1a836du     /* part B */
#define FOLDER_MIX      0x7b1a82adu     /* part B: mix() */
#define FOLDER_PREV     0x7b1a8755u     /* part D */
#define FOLDER_SONG     0x7b1a878du     /* part D */
#define PREV_CROSS      0x7b0a82adu     /* stock: previous song, crossing folders */
#else
#define RAM(off)        (TEST_RAM + (off))
#define VFS_STUB        TEST_VFS
#define RAND_STUB       TEST_RAND
#define RESET_ROOT      TEST_RESET_ROOT
#define COUNT_FILES     TEST_COUNT_FILES
#define SEEK_FILE       TEST_SEEK_FILE
#define NEXT_CROSS      TEST_NEXT_CROSS
#define WALK            TEST_WALK
#define FOLDER_RESEED   TEST_FOLDER_RESEED
#define FOLDER_NEXT     TEST_FOLDER_NEXT
#define FOLDER_MIX      TEST_FOLDER_MIX
#define FOLDER_PREV     TEST_FOLDER_PREV
#define FOLDER_SONG     TEST_FOLDER_SONG
#define PREV_CROSS      TEST_PREV_CROSS
#endif

#define VFS(m, e, f)    (((u32 (*)(u32, u32, u32, u32, u32))VFS_STUB)(HANDLE, (m), 0, (e), (f)))
#define CD(c)           ((u8)VFS((c), 0, 0))
#define RAND()          (((u32 (*)(u32, u32, u32, u32))RAND_STUB)(0, 0, 0, 0x35))

/* Engine folder-mode state (addresses from the stock walker and seek code). */
#define SHUFFLE         (*(volatile u8 *)RAM(0x23))
#define LAYER           (*(volatile u8 *)RAM(0x22))
#define POS             (*(volatile u16 *)RAM(0x24))
#define COUNT           (*(volatile u16 *)RAM(0x26))
#define HANDLE          (*(volatile u32 *)RAM(0x34))
#define AUDIO_EXT       (*(volatile u32 *)RAM(0x3c))
