/* Loader for the file-backed index passes.  Replaces FUN_400d5290 in playlist.ap, at
 * 0x400d5290 in bank 0x400d4c00, which the scan driver FUN_40055194 calls after the scan.
 *
 * The passes (index.c, linked at 0xbfc3c000) are too big for one 2 KB bank, so they are
 * stored as data in free bank space (see patch_library.py for the layout) and copied into
 * the top of the builder's RAM before running.  Calling into a bank makes the kernel load
 * it, so every chunk outside this bank starts with a "jr ra; nop" stub to call.  Every
 * 0x40xx4c00 bank is loaded into the same 2 KB window as this one, so no chunk may live in
 * another bank of that group: loading it would pull the loader out from under itself (the
 * build with chunk 3 in bank 0x40254c00 hung at 20%).  The 0x48 and 0x71 groups have their
 * own windows.
 *
 * The copy goes through the uncached alias and runs from it, like the apps themselves.
 * Before copying, the builder's RAM is read once through the cached alias so that dirty
 * cache lines the scan left there are written back first and can't land on the code later.
 *
 * load_and_run must be the first function in the file: the stock caller jumps to the start of
 * the blob, and GCC lays static functions out in source order, so the helper is declared here
 * and defined after it.  build.sh checks the entry address.
 */
typedef unsigned int u32;
typedef unsigned char u8;

#define DST   0xbfc3c000u
#define ENTRY 0xbfc3c001u

#define C0_SRC 0x400d4c00u                /* this bank, before the loader: 1680 bytes */
#define C0_LEN 0x690u
#define C1_SRC 0x71252500u                /* an unused bank: stub + 2044 bytes */
#define C1_LEN 0x7fcu
#define C2_SRC 0x400d5370u                /* this bank, after the loader: 144 bytes */
#define C2_LEN 0x90u
#define C3_SRC 0x480946a0u                /* the zero tail of bank 0x48094400: stub + 1372 bytes */
#define C3_LEN 0x560u
#define C4_SRC 0x485946c0u                /* the zero tail of bank 0x48594400: stub + 1340 bytes */
#define C4_LEN 0x540u

static void chunk(u32 src, u32 len, u32 dst, int call);

void load_and_run(void)
{
    u32 sum = 0;
    const volatile u32 *p;
    for (p = (const volatile u32 *)0x9fc20000u; p < (const volatile u32 *)0x9fc40000u; p++) sum += *p;
    *(volatile u32 *)0xbfc3bffcu = sum;         /* keep the reads */
    chunk(C0_SRC, C0_LEN, DST, 0);
    chunk(C1_SRC, C1_LEN, DST + C0_LEN, 1);
    chunk(C2_SRC, C2_LEN, DST + C0_LEN + C1_LEN - 4, 0);
    chunk(C3_SRC, C3_LEN, DST + C0_LEN + C1_LEN - 4 + C2_LEN, 1);
    chunk(C4_SRC, C4_LEN, DST + C0_LEN + C1_LEN - 4 + C2_LEN + C3_LEN - 4, 1);
    ((void (*)(void))ENTRY)();
}

static void chunk(u32 src, u32 len, u32 dst, int call)
{
    const u8 *s;
    u8 *d = (u8 *)dst;
    if (call) {
        ((void (*)(void))(src | 1))();
        src += 4;
        len -= 4;
    }
    for (s = (const u8 *)src; len; len--) *d++ = *s++;
}
