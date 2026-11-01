# How the MECHEN M30 builds its music library

This note explains how the stock 2025-06-26 firmware (`MECHEN_M30.HEX`) scans the card and
builds `MUSIC.LIB`, why it stops at 4,000 songs, and what it would take to index a whole card
(about 18,000 tracks). It records the analysis done on 2026-11-01 from the decompiled
`playlist.ap`, `music.ap`, and the file system drivers. Addresses are load addresses. File
offsets are into the unpacked `.ap` files. `N` is the number of songs found.

Related notes: `README.md` (firmware format, banks, patch sites) and the hand-off note in
`notes/library-4000-cap-prep.md` (git ignores `notes/`).

## Summary

- The library builder runs inside `playlist.ap` during "filelist update". It scans the card
  with the file system's directory walker, writes one 256-byte record per song, then runs four
  index passes (All songs, Album, Artist, Genre) that sort and group the records and append the
  index tables to `MUSIC.LIB`.
- The 4,000 cap is a RAM budget, not a format limit. The builder owns 120 KB of RAM from
  `0x9fc22000` to `0x9fc40000` and fills it with seven 8 KB `u16` tables (4,096 entries each),
  a 4 KB track-number array, and a 64 KB sort-key area. Every count and sector pointer in the
  file is 16 bits, so the format itself allows tens of thousands of songs.
- Raising the constants in place caps out near 6,500 songs, because the stock algorithm needs
  about 15 bytes of RAM per song plus the sort keys. Whole-card indexing needs a rewrite of the
  four index passes that keeps the per-song tables in the file instead of RAM. The scan, the tag
  reader, the record writer, the node writer, and the header writer can stay as they are.
- Sort quality is not a concern. The stock code already refines ties by re-reading longer keys
  from the file, so short keys only cost time.
- `.m4a` files are scanned and tagged. The stock sample library built from this card holds 210
  M4A records, 207 of them with tags.
- The reader-side 4,000 checks in `music.ap`, `browser.ap`, and `mainmenu.ap` are upper bounds
  on linear searches that map a file location back to a library index. They don't limit
  browsing, but they must be raised so songs past 4,000 can be found from the player screens.

## playlist.ap layout

`playlist.ap` differs from the other apps: it has two resident segments. The header words at
`0x08`, `0x0c`, `0x10` give file offset `0x1400`, size `0x7bc`, load address `0xbfc16400` for the
normal resident segment, and the words at `0x14`, `0x18`, `0x1c` give file offset `0xc00`, size
`0x800`, load address `0xbfc11d00` for a second one. `browser.ap` and `music.ap` only have the
first triple, with the resident code at file `0xc00`. The README's statement that the resident
code is always at file `0xc00` is only true for those apps.

The syscall stubs (`li v1,MODULE; break`) sit at both ends of the first segment:

| Address | Module | Used for |
|---|---|---|
| `0xbfc16401` | `0x0c` | misc |
| `0xbfc16405` | `0x09` | misc |
| `0xbfc16409` | `0x10` | string and memory: `0` copy, `1` fill, `3` length |
| `0xbfc1640d` | `0x11` | file system: `0xb` seek, `0xd` read, `0xe` write, `5` open, `6` create, `7` close, `8` change dir, `9` size, `0x13` set size, `1` next entry, `0x2f` enter dir, `0x7c` leave dir, `0x80`/`0x82` next sibling or child |
| `0xbfc16bad` | `0x07` | misc |
| `0xbfc16bb1` | `0x0f` | kernel |
| `0xbfc16bb5` | `0x06` | text conversion: `0x1a` UTF-16 to multibyte, `0x1d` UTF-8 to UTF-16 |
| `0xbfc16bb9` | `0x00` | misc |

Resident functions the builder calls directly:

| Address | File | What it does |
|---|---|---|
| `0xbfc16411` | `0x1410` | Append write: `(buf, len)` writes `ceil(len / 512)` sectors of 512 bytes with file system op `0xe` and adds that many sectors to the sector counter at `0x9fc1540a`. |
| `0xbfc165e5` | `0x15e4` | Message pump: polls keys and USB events, resets a watchdog counter, and sends kernel message `0x31` every fifth call. Loops call it so the player stays responsive. |
| `0xbfc1687d` | `0x187c` | Directory walker: returns the next file as a packed extension (`MP3\0`), walking subdirectories depth first to a depth of 8. Uses `0xbfc16808` for the next file in the current directory and `0xbfc169b4` to skip the `RECORD` folder. |
| `0xbfc16785` / `0xbfc167d9` | `0x1784` / `0x17d8` | Walker setup: stores the extension mask at `0xbfc16ae8`, resets the depth and counter at `0xbfc16ae0` and `0xbfc16ae2`, and returns to the root. |

The bank table has five groups of 64 entries, from `0x100` to `0x1000`. The header word at
`0x2c` says `0x900`, so `tools/apinfo.py` and `tools/ghidra/LoadAP.java` stop before the fourth
group. Group 4 at `0xa00` holds the shared-library banks at `0x71xx2500`, which carry the tag
parsers, the M3U code, and the sort routine. To decompile them, scan the table to `0x1000` and
map the two resident segments as described above.

The app's data segment is at `0x9fc15400`, size `0x6f4`. Every `0x9fc15xxx` pointer below is an
app variable at `0x9fc15400 + offset`.

## RAM map during the build

The chip has 256 KB of RAM at physical `0x1fc00000` to `0x1fc40000`, seen as `0x9fc00000`
(cached) or `0xbfc00000` (uncached). Apps run uncached at `0xbfc16400`. The builder's buffers,
set up by `FUN_40055068` (bank `0x40054c00`, file `0x2868`), are:

| Variable | Buffer | Size | Stock use |
|---|---|---|---|
| `0x9fc154d4` | `0x9fc22000` | 8 KB | Record staging buffer during the scan: 32 records, flushed with the append write. |
| `0x9fc154e8` | `0x9fc22000` | 8 KB | A: the sort order (a permutation of song indexes). Reuses the staging buffer after the scan. |
| `0x9fc154ec` | `0x9fc24000` | 8 KB | B: the inverse of the previous pass's order. Doubles as the secondary sort key. |
| `0x9fc154e0` | `0x9fc26000` | 8 KB | C: the per-song group number from the first grouping step. |
| `0x9fc154f0` | `0x9fc28000` | 8 KB | E: group run lengths (`u16` per group, 0 terminated). Also the per-song "bytes compared" array during tie refinement. Sector-sized scratch buffers at `0x9fc28400` to `0x9fc28e00` are used by other phases. |
| `0x9fc154d8` | `0x9fc29000` | 4 KB | Track numbers, one byte per song, written during the scan. |
| `0x9fc154e4` | `0x9fc2a000` | 8 KB | H: second-level run lengths (albums within artists). |
| `0x9fc154f4` / `0x9fc154fc` | `0x9fc2c000` | 8 KB | F: scratch permutation for tie refinement, then the per-song node index output. |
| `0x9fc154f8` / `0x9fc154dc` | `0x9fc2e000` | 8 KB | G: the "same key as previous" array, and third-level run lengths. |
| `0x9fc154d0` | `0x9fc30000` | 64 KB | Sort keys. The scan writes title keys here. Each index pass overwrites it with that pass's field keys. |

Every `u16` table is 8 KB, so 4,096 entries. The key length is `0x10000 / N` capped at 64 bytes
(`FUN_40095160`), which keeps the keys inside 64 KB. The scan loops stop at 4,000 because that
fills the budget exactly: the seven tables take the 56 KB from `0x9fc22000` to `0x9fc30000`
(the track array sits inside one of the slots), and the keys take the 64 KB above them.

The storage drivers (`card.drv`, `cards.drv`, `nor_ud.drv`) reference `0x9fc28000` to
`0x9fc29300`, and the UI driver and codecs use `0x9fc33000` to `0x9fc3d000` at other times. The
stock builder already keeps live arrays at `0x9fc28000` across file system reads, so the card
driver doesn't clobber that region during the build.

## Build flow

`FUN_40054c54` (bank `0x40054c00`) is the "filelist update" driver. For each library type
(MUSIC.LIB, then M3U.LIB) it:

1. Calls `FUN_40055068` to set the buffer pointers above.
2. Calls `0xbfc167d9` with the extension mask for that library type and resets the walker.
3. Creates the output file with `FUN_40194c78` (bank `0x40194c00`): deletes a stale file, creates
   it with a fixed size from the table at `0x40194fa8`, which is `0x281000` (2,625,536 bytes, or
   64 KB plus 4,000 times 640) for MUSIC.LIB, and writes a 512-byte header with the section
   names `ALLSONG`, `ALBUM`, `ARTIST`, and `GENRE`. Nothing in this step knows `N`.
4. Clears the section tables (`FUN_40055108`) and runs the builder for the type: `FUN_40055194`
   for MUSIC.LIB, `FUN_400551d0` for M3U.LIB.
5. Finalizes the header with `FUN_40194fe4` and `FUN_4019502c`: total count at `0x08`, and per
   section the three counts and the sector pointers.

`FUN_40055194` runs the scan (`FUN_40095220`), the progress display (`FUN_401d4cb0`), and the
index passes (`FUN_400d5290`).

### Extension mask

The mask table at `0x40055220` (file `0x2a20`) holds `0x7e190ea0` for MUSIC.LIB and
`0x00080400` for M3U.LIB. The mask selects entries of the file system driver's extension table
(`exfatn.drv` file `0x7f4`, `fat_32n.drv` file `0x77c`): MP3, WMA, WAV, AAC, FLAC, APE, AVI, TXT,
JPG, JPEG, DSF, DFF, LRC, ASF, OGG, TTA, ISO, AAX, AA, MP1, MP2, M4A, POS, ACT, BMK, AIF, and
more. The MUSIC.LIB mask has 17 bits set, matching the 17 audio formats, and the sample library
confirms M4A is included. The bit order inside the driver isn't decoded. The folder views in
`browser.ap` and `music.ap` OR in the literal `0x00400000` separately.

### Scan: FUN_40095220 (bank 0x40094c00, file 0x3220)

Two passes over the walker. Pass 1 only counts files into `0x9fc15440` and stops at 4,000
(`sltiu v0,4000` at `0x4009523e`, file `0x323e`). It then calls `FUN_40095160` to set the key
length and field widths, and changes back to the root (`cd ':'`). Pass 2 walks again and, per
file, stops at 4,000 (`0x400952c8`, file `0x32c8`):

- `FUN_40094c00` upper-cases the 3-byte extension.
- Every eighth file it calls the message pump.
- It clears the 256-byte record at `0x9fc15530` and the 8-byte track string at `0x9fc15860`.
- `FUN_40094f2c` maps the extension to a tag-reader type with `FUN_48014400`: 0 AA, 1 AAC,
  2 M4A and AAX, 3 APE, 4 FLAC, 5 MP1, MP2, and MP3, 6 OGG, 7 WMA, ASF, and WMV, 8 anything
  else. Types below 8 go to the tag reader at `0x7805b201` (bank `0x7805b200`, file `0xb400`),
  which fills the record and sets the tag-ok flag at `+0xa3`. Type 8 and failed reads fall back
  to `FUN_40094d60`: the title is the 8.3 short name, the artist, album, and genre become
  "No Artist", "No Album", and "No Genre", and the track number is parsed from the name. Then it
  asks the file system for the file location (`+0xa9` and `+0xf4`) and sets `+0xa8` to 1.
- `FUN_40094fec` writes the record: copies the title key (key length bytes, through the text
  converter) to the key area, appends the record to the staging buffer, flushes every 32 records
  through `0xbfc16411`, sets the prev and next links at `+0xfc` and `+0xfe`, and stores the low
  byte of the track number in the track array.

Pass 2 ends by flushing the partial staging buffer. The M3U scanner `FUN_40154ecc` has the same
shape with a per-type limit table.

### Record format

Records start at file offset `0x200`, 256 bytes each:

| Offset | Size | Field |
|---|---|---|
| `0x00` | 64 | Title |
| `0x40` | 24 | Artist |
| `0x58` | 48 | Album |
| `0x88` | 24 | Genre |
| `0xa0` | 3 | Track string |
| `0xa3` | 1 | Tag-ok flag |
| `0xa4` | 4 | Extension as packed text (`MP3\0`) |
| `0xa8` | 1 | Always 1 |
| `0xa9` | | File location: exFAT directory cluster and entry offset |
| `0xf2` | 2 | Track number, `0xffff` if none |
| `0xf4` | 8 | File location, second part. The reader-side lookups compare these 8 bytes. |
| `0xfc` | 2 | Previous record index |
| `0xfe` | 2 | Next record index |

Text is UTF-16LE with an `FF FE` BOM, or plain ASCII for the placeholders.

### Index passes: FUN_400d5290 (bank 0x400d4c00, file 0x3a90)

The driver clears A, saves `N`, and runs the four passes, each followed by the message pump and
a progress update (`FUN_401d4cb1(0xff)`). The helper banks are `0x40114c00` (key extraction and
tie refinement), `0x40294c00` (identity, grouping, inverse, node writer), and `0x71812500` (the
sort and compare routines).

All songs, `FUN_400d4c00`:

1. `FUN_40294c00`: A[i] = i.
2. Sort A by the title keys the scan wrote (`0x71812875`).
3. `FUN_40114d54` refines ties (see below).
4. `FUN_40294d74`: B[A[i]] = i, the inverse.
5. Append A (2N bytes) and B (2N bytes) to the file and store their sectors in the section
   header.

Album, `FUN_400d4ce0`:

1. A[i] = i, then `FUN_40114c00` reads the album field of every record from the file, converts
   it, and writes a key of up to `0x30` bytes per song.
2. Sort A by key, refine ties.
3. `FUN_40294c38`: compute E, the run length of each album group, and C, the group number per
   song.
4. `FUN_40294db4`: sort inside each group with compare mode 1 (track number, then title rank B).
5. `FUN_40294f20`: write the album level nodes.
6. `FUN_40294d74`: B becomes the inverse of the album order (album rank).
7. `FUN_40294ec8`: F[A[i]] = group index, the per-song node index.
8. Append A, B, and F.

Artist, `FUN_400d4e80`: the same with 24-byte artist keys, compare mode 0 inside groups (album
rank), two levels of run lengths (E artists, H albums within artists), two node tables, and three
per-song tables. Genre, `FUN_400d5064`: 24-byte genre keys, compare mode 0 (artist rank), three
levels, three node tables, three per-song tables.

### Sort, compare, and refinement

`FUN_71812874` (bank `0x71812500`, file `0xa400`, shared by every app) is an in-place quicksort
over a `u16` array with an explicit stack of 8-byte frames at `0x71812b64` and `0x71812c2c`,
insertion sort below 9 elements, and a compare mode:

| Mode | Function | Orders by |
|---|---|---|
| 0 | `FUN_71812650` | the `u16` at `*0x9fc154b8 + 2 * index`, that is the previous rank in B |
| 1 | `FUN_718126c0` | the byte at `*0x9fc154b0 + index` (track number), then mode 0 |
| 2 | `FUN_71812548` | the keys at `*0x9fc154b8 + keylen * index`, byte-wise, ASCII upper-cased, up to 100 bytes |

The sort has no count limit of its own; its only size inputs are the `u16` count and the key
length byte. The element stride is 2 bytes.

`FUN_40114d54` (bank `0x40114c00`, file `0x3d54`) makes the order exact after a sort with
truncated keys. It computes G, where G[i] counts how many previous keys equal key i, and keeps a
per-song byte array of how many bytes of the field were compared so far. Then it loops: for each
run of equal keys longer than one song whose compared length is below the field width, it
re-extracts keys from the file for that run only, starting at the already-compared offset, with a
length of `0x10000 / run` capped at the remaining width and at 100 bytes, sorts the run with a
scratch permutation in F, updates G, and repeats until no run can be refined further. This is why
the stock library is sorted on whole fields even though the initial keys are 16 bytes at 4,000
songs.

`FUN_40114c00` extracts keys: for each song in A order it seeks to `0x200 + 256 * index + field`
(file system op `0xb` with a byte offset) and reads the field width (op `0xd`) into
`0x9fc15638`, then converts it through `FUN_48014694`.

### Text conversion for keys

`FUN_480145dc` (bank `0x48014400`, file `0x75dc`) normalizes a field: UTF-16 with BOM goes
through module 6 function `0x1a` (UTF-16 to multibyte), UTF-8 with BOM goes through `0x1d` then
`0x1a`, and plain ASCII is copied as is. `FUN_48014694` then copies the first `keylen` bytes of
the result from `0x9fc15538` into the key slot. Keys are therefore one byte per character for
ASCII text, and the compare upper-cases them.

### Node writer: FUN_40294f20 (bank 0x40294c00, file 0x6f20)

Writes level nodes of 16 bytes through a 512-byte buffer at `0x9fc15538`, flushing every 32
nodes with the append write. Inputs are A, this level's run lengths, the child level's run
lengths (to count children per node), the parent level's run lengths (to compute the parent
index), and the depth byte. Node layout: `u8 1`, `u8 depth`, `u16 children`, `u16 songs`,
`u16 first song record`, `u16 first child`, `u16 parent`, `u16 prev`, `u16 next`. `0xffff` means
none. It returns the node count.

## MUSIC.LIB layout

The file is one 512-byte header, then `N` records, then per section the node tables and three
per-song tables, all appended in order. Each append rounds up to whole sectors. The header holds
`MUSIC`, the `u16` total at `0x08`, the current sector at `0x0a`, and four 28-byte sections at
`0x10` `ALLSONG`, `0x2c` `ALBUM`, `0x48` `ARTIST`, and `0x64` `GENRE`: `name[8]`, `u16 counts[3]`,
`u16 sectors[7]`. Sector slots 0 to 2 point to the level node tables and slots 3 to 5 to the
per-song tables: the sort order, the inverse rank, and the owning node index.

The stock sample in `notes/MUSIC.LIB.stock-4000` (4,000 songs, 336 albums, 110 artists, 105
genres) lays out as: records in sectors 1 to 2000, ALLSONG order at 2001 and inverse at 2017,
ALBUM nodes at 2033, tables at 2044, 2060, 2076, ARTIST nodes at 2092 and 2096, tables at 2107,
2123, 2139, GENRE from 2155. Decode a file with `python3 tools/musiclib.py FILE`.

For 18,000 songs the records take 4.5 MB and each per-song table 36 KB, so the file needs about
5 MB plus scratch. The fixed preallocation of 2.5 MB has to grow. Sector pointers are `u16`, so
the file can reach 32 MB, which is enough for about 40,000 songs.

## Reader side

`music.ap`, `browser.ap`, and `mainmenu.ap` carry the same three functions in the shared banks
`0x712d2500` and `0x71352500`:

- `FUN_712d27f4`: linear search of the library records for the one whose 8-byte location at
  `+0xf4` matches the given file. Stops at index 3,999 and reports failure at 4,000.
- `FUN_71352810`: the same search over a user playlist's 8-byte entries, read 64 at a time from
  sector `0xb15` on.
- `FUN_713528ec`: removes a song from the user playlists; its inner loop also stops at 4,000.

These resolve "which library song is playing" for favourites and playlist bookkeeping. With more
than 4,000 songs they fail for the later songs, so the literals must be raised. Each site is a
MIPS16 `sltiu` with an `EXTEND` prefix, for example `a1f7 005a` at `music.ap` file `0x19626`:
`EXTEND 0xf7a1` plus `sltiu v0` gives the immediate 4,000. `music.ap` also has 32-bit literals of
4,000 at file `0x435c` and `0x4368` that are not yet explained.

Browsing itself reads the `u16` counts from the header and the tables by sector, so it has no
separate limit that is known yet. This is the first thing to verify on the player once the
builder writes more than 4,000 songs.

## Why raising the cap in place is not enough

With the stock algorithm, the live data per song during the worst pass is: A, B, G, and F at 2
bytes each, the compared-bytes array and the track array at 1 byte each, and the keys. That is
10 bytes plus the key length, and the group tables and node-index output come on top. Even with
2-byte keys and every table laid end to end, 120 KB holds about 7,000 songs. At 18,000 songs the
tables alone need more than 250 KB, which is the whole chip.

The format doesn't need any of these tables in RAM at once. The sort order A is the only table
the node writer and the grouping walk need in RAM, and at 18,000 songs it is 36 KB. The inverse
and the node index are random-access outputs that can be built in 8 KB chunks by walking A, and
the previous pass's inverse and node index, which the next pass needs as secondary keys, are
sequential reads from the file.

## Recommended approach

Keep the stock scan, tag reader, record writer, node writer, and header writer. Replace the four
index passes with a file-backed sort:

1. Raise the two scan loop bounds and the preallocation size to a new maximum such as 24,000.
   The scan's own buffers scale: the key area holds `0x10000 / N` bytes per song, the track array
   at `0x9fc29000` reaches `0x9fc2edc0` at 24,000, and the staging buffer is unchanged.
2. For each pass, extract fixed-size key records to a scratch area at the end of `MUSIC.LIB`:
   the converted field, the previous pass's rank and node index, the track number, and the song
   index. Sort them with an external merge sort: in-RAM runs sorted with the stock quicksort, then
   k-way merges with 2 KB buffers. Composite keys replace the stock sub-sorts and tie refinement.
3. Walk the sorted stream once to fill A and the run-length tables, call the stock node writer,
   then build the inverse and node-index tables in chunks and append them.

RAM at 18,000 songs is about 36 KB for A, 16 KB for run lengths, and 20 KB of I/O buffers.
Scratch in the file is about 2.6 MB per pass. The design works up to about 32,000 songs with A in
RAM, and higher if A is streamed too.

The new code is 4 KB to 8 KB of MIPS16. `playlist.ap` has about 49 KB of zero tail across its
banks: the whole bank at `0x71252500` (file `0x10400`) is empty, and `0x40254c00`, `0x401d4c00`,
`0x48094400`, `0x48594400`, `0x48014400`, `0x48c94600`, `0x40014c00`, and `0x71292500` each have
more than 1.2 KB free. Bank `0x400d4c00` becomes free once the stock passes are replaced.
`tools/repack_fw.py` cannot grow a file inside the firmware image, so the code must fit in these
gaps. Because banks are 2 KB windows and calls between banks of one group reload the window, the
plan is to store the new code as data in the free bank space, copy it to RAM at the top of the
builder's region with a small loader in bank `0x400d4c00`, and run it from the uncached alias,
which is how the apps themselves run.

Everything else stays stock: the M3U.LIB scanner and its 4,000-entry limit, the tag reader, and
the folder views.

## Test plan

1. A QEMU user-mode harness, like `tools/playall/harness.c`, that loads the stock banks
   `0x400d4c00`, `0x40114c00`, `0x40294c00`, `0x71812500`, `0x48014400`, and `0x40194c00` at
   their real addresses, moves every RAM and resident pointer in their literal pools to harness
   memory, and implements the file system ops, the text conversions, the append write, and the
   message pump in C. Running the stock passes on the records from
   `notes/MUSIC.LIB.stock-4000` must reproduce that file's tables. This proves the harness and the
   reading of the stock code.
2. The same harness runs the new passes on the same records and diffs the output against stock.
3. A synthetic 18,000-song input built from the card index checks the new passes at scale
   against a Python reference model, and `tools/musiclib.py` decodes the result.
4. On the player: raise the cap, flash, and check All songs, Artists, Albums, Genres, favourites
   from the now-playing screen, and the filelist update time. The update already takes minutes
   for 4,000 files, and tag reading dominates, so expect a longer wait after each unplug.

## Risks

- The filelist update runs before the menu after every unplug and blocks USB. A builder that
  crashes could stop the player from reaching Setting > Auto upgrade, the only known flashing
  path. A marker file on the card as a kill switch, or falling back to the stock 4,000 behavior
  when the new code fails a self-check, is worth building in.
- The exact bit order of the extension mask, the two unexplained 4,000 literals in `music.ap`,
  and whether the list screens have a hidden limit are open until the first on-device test.
