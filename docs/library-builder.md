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
- Done (2026-11-03): the patched `playlist.ap` builds a 15,991-song library on the player from
  a 256 GB card, and every table in it is consistent and matches the PC model except for the
  placement of names with characters above U+00FF. Getting there took four rounds against the
  kernel's file I/O, described under "What the player runs showed".
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

`FUN_480145dc` (bank `0x48014400`, file `0x75dc`) normalizes a field: UTF-16 with BOM is copied
(twice the field width in bytes, so the copy runs past the field into stale read-buffer bytes)
to `0x9fc15538` and converted in place by module 6 function `0x1a` (UTF-16 to multibyte), UTF-8
with BOM goes through `0x1d` then `0x1a`, and plain ASCII is copied as is. `FUN_48014694` then
copies the first `keylen` bytes of the result into the key slot. Keys are therefore one byte per
character for ASCII text, and the compare upper-cases them.

The conversion isn't decompiled (it lives in the kernel), but the sort order of a player-built
library pins most of it down. Characters up to U+00FF become their byte value, so "Déjà vu" sorts
after "Dross Glop" and "02 Qué Onda" after "02 Queen". The first character above U+00FF stops the
conversion without writing anything, so the rest of the buffer keeps the UTF-16 input bytes: that
is why "“Awaken, My Love!”" (U+201C, bytes `1c 20`) and "…And You Will Know Us" (U+2026, bytes
`26 20`) are the first two albums, before "13". Three cases don't fit any per-string rule tried
(the all-Greek album "Ἀποκάλυψις" sorts third instead of first, "We’ve Come for You All" sorts
before "We Got It From Here", and "D’Angelo" and "DāM‐FunK" are separate artists), so the
player's handling right after such a character has state this sample doesn't reveal. It affects
only names with characters above U+00FF.

The record field for a 23-character UTF-16 name has no terminating zero, and the tag reader seems
to limit names to 22 characters plus the terminator.

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

## The new index passes

`tools/library/index.c` replaces the four stock passes. The scan, the tag reader, the record
writer, the header writer, and the text conversion stay stock. The design is shaped by one
number measured on the player: a kernel file call costs about 10 ms, whatever its size (the
stock update takes about 8 minutes for 4,000 songs, or 0.1 s per file for roughly ten calls
each). So the passes are arranged to make as few calls as possible:

- The scratch data lives in a second file, `MUSIC.TMP` in the card root, opened with its own
  handle. A leftover from an interrupted update is deleted first (by its long name, which
  works on the exFAT card, and by its 8.3 name), because the kernel cannot grow a file past
  the size it was created with and a leftover may be too small. Then it is created with the
  same calls the stock uses for `MUSIC.LIB` (change to the root, create with a size, op
  `0x13` with 2, close, open), and at the end it is closed and deleted. A create on an
  existing name adds a second directory entry on exFAT, which is why the delete comes first.
  `MUSIC.LIB` is then only read sequentially and appended to, and the two handles never seek
  past each other. If the file can't be created, the scratch area goes at the end of
  `MUSIC.LIB` instead, with the same code.
- Every stream has a buffer of 8 or 16 KB, and a seek is issued only when a handle's
  position isn't already right, so sequential streams cost one call per buffer.
- Every file call starts on a sector boundary, moves whole sectors, and stays inside one
  4 KB block (`fs_io` splits a transfer at block boundaries). The kernel gets both other cases
  wrong, as the diagnostic runs below showed, and 4 KB is the smallest cluster size the code
  expects on a card. Sort runs are padded to sector boundaries, readers fetch aligned windows
  and skip into them, and writers pad their last sector.
- The merge is one pass of up to 32 runs with 2 KB buffers each (a run is 48 KB of records
  sorted in RAM, so 32 runs cover 21,000 titles; above that a second pass runs).
- The record table is read once: the title pass also writes the converted album, artist, and
  genre names (with the track byte) to three key files, which the later passes read back
  instead of the 256-byte records.
- One walk of the sorted stream writes both the sort order and the run lengths of every level
  (by name, then by album group, then by artist group). The level nodes are then written from
  the order table and the run lengths, with a reimplementation of the stock node writer
  (`write_nodes`), and the per-song tables (inverse rank, node index of the deepest level, and
  for the Artist pass the level-0 artist group) are built in 8 KB chunks of 4,096 songs by
  walking the order table once per chunk, with the inverse rank and the groups also kept in
  the scratch file for the next pass.

The key record is the converted name (64, 48, 24, or 24 bytes, through the stock converter
`FUN_480145dc`), the track number byte, the song's rank in the previous section's order, the
album and artist group of the song, and the song index. The compare is the stock one (ASCII
upper-cased, stopping at a shared terminator), then the track number as a signed byte inside an
album (as the stock mode 1 compare), then the previous rank, then the song index. Since the
compare is total, the stock's separate sub-sorts and tie refinement aren't needed.

RAM use is fixed: a 96 KB arena at `0x9fc22000` laid out per phase (records and writers, the
48 KB run buffer and its index arrays, the 32 merge readers, the chunk arrays), the merge
slots and reader state at `0x9fc3a000`, and the globals at `0x9fc3ae00`. The code is 5.2 KB
of MIPS16 and runs at `0xbfc3c000`. Nothing scales with `N` except the files: the scratch
file holds two `76 N` record areas, the three key files (`102 N`), three run-length streams,
the order, and three per-song tables, about `270 N` in all (4.8 MB at 18,000 songs), and the
`MUSIC.LIB` preallocation is 64 KB plus 660 bytes per song at the limit, enough for the
scratch area in the fallback case.

In the harness at 18,000 synthetic songs the passes make 3,006 seeks, 3,724 reads (19 MB) and
1,768 writes (14 MB): about 8,500 kernel calls, against 137,000 for the first version, which
kept everything in `MUSIC.LIB` with 2 KB buffers and 512-byte calls.

Two differences from stock are deliberate. Songs whose names compare equal keep their scan
order (the stock quicksort scrambles them), and a name stored as ASCII in one record and as
UTF-16 in another is one group. The stock makes two groups out of such a pair: after the
terminator its key buffer holds leftover UTF-16 bytes for one record and zeros for the other,
and the tie refinement sees the difference. The player-built sample has seven such duplicate
albums, four artists, and two genres.

### Loader and patch layout

`tools/library/loader.c` takes the place of `FUN_400d5290` at `0x400d5290`. It reads the
builder's RAM once through the cached alias, so dirty cache lines left by the scan are written
back before the code lands there, then copies the passes from four pieces of free bank space to
`0xbfc3c000` through the uncached alias and calls them. Calling into a bank makes the kernel
load it, so the pieces outside the loader's own bank start with a `jr ra; nop` stub that the
loader calls first. The loader's entry must be the first function in its file: the stock
caller jumps to the first byte, and the blobs are compiled with `-fno-toplevel-reorder`, so
functions keep their source order. The first two builds had the static helper `chunk` defined
before `load_and_run`, so the scan driver called `chunk` with its own arguments and the player
hung at 20% in every run; `build.sh` now checks the entry address of both blobs, and the
patch script reads the pieces back. The blob is whatever the linker puts in `.text`, so
`build.sh` links `.rodata` (string literals, the hex table) into it right after the code: the
first diagnostic run had every string missing, its result read "819281920" with no labels,
and the scratch file name was garbage. `tools/library/patch_library.py` writes the pieces:

| Piece | Where | Bytes |
|---|---|---|
| 0 | bank `0x400d4c00`, offset 0 to `0x690` (the dead stock passes) | 1,680 |
| 1 | bank `0x71252500` (file `0x10400`), unused in stock | 2,044 after the stub |
| 2 | bank `0x400d4c00`, offset `0x770` to `0x800`, after the loader | 144 |
| 3 | the zero tail of bank `0x48094400` (clears the update flag when the build is done) from offset `0x2a0` | 1,372 after the stub |
| 4 | the zero tail of bank `0x48594400` (user playlist file helpers) from offset `0x2c0` | 1,340 after the stub |

A chunk must not share the loader's bank window. Every `0x40xx4c00` bank is loaded into the
one 2 KB window that holds the loader's own bank `0x400d4c00`, so calling the stub of a chunk
stored in another bank of that group replaces the loader's code while it runs. One build kept
piece 3 in the tail of bank `0x40254c00` and hung at 20% in every run, with the records
scanned and no `MUSIC.TMP` created. The `0x48` group (window `0x4400` to `0x4c00`) and the
`0x71` group (window `0x2500`) are separate windows, which is also why the stock code can
call between the groups freely. `patch_library.py` checks the group of every chunk.

A zero tail is not always free. The first whole-card builds kept piece 3 in the zero tail of
bank `0x71292500`, and every short M4A name in those libraries came out with junk appended
("Dirt" became "DirtQQ\x02JJ5#..."; 391 of 515 M4A records in one run, none in the stock
library). That bank holds the M4A reader's UTF-8 string helper (`FUN_71292644`: read up to 128
bytes of the atom, convert them with `FUN_71292500`, copy the result into the record field),
and its two scratch buffers are the tail itself, at `0x7129272c` and `0x712927ac`. The
converter runs until it meets a zero byte, so whatever is stored after the string is read as
part of the name, and the junk was the passes' own code. A literal-pool scan of the file
(every 32-bit word that looks like a bank address) finds those two pointers and none into the
tails of `0x48094400` and `0x48594400`, whose code keeps its state on the stack and in the
app's data segment; `patch_library.py` asserts both tails are zero in the stock file. Neither
bank runs during the scan, so the window cannot hold a modified copy when the loader reads it.

The same patch raises the two scan limits at `0x4009523e` and `0x400952c8` and the
preallocation literal at `0x40195260`, and makes two more changes to the stock code:

- `FUN_40194c78` (the library create) opens an existing `MUSIC.LIB`, and if it is smaller
  than the preallocation closes it and calls create on the name. The stock never hits that
  path (its file is always the preallocated size), but the first boot after flashing does, and
  on the exFAT card the create on an existing name adds a second `MUSIC.LIB` directory entry
  (the first run left two, which `fsck.exfat` repaired by renaming one; the scan had written
  its records into the old file, extending it). Deleting the old file first on that path was
  tried and changed nothing (a second run left a second pair), although the same delete call
  does remove `MUSIC.TMP` at the end of the passes; the difference is not understood (the
  library may still have been open). So the `bteqz` at `0x40194d0e` that skips
  the close when the file is big enough is now an unconditional `b`: an existing library is
  kept whatever its size, and the scan's writes extend it. The delete calls still run on the
  file-missing path, as in stock.
- The scan's counting pass (the first loop of `FUN_40095220`) only sized the stock's 64 KB key
  buffer, which the new passes never read. Its first walker call at `0x4009522a` is now
  `li v0,0`, so it ends at once, and the scan walks the card once instead of twice. The key
  width becomes 0, so the stock key extraction copies and converts nothing.

`--reader` rewrites the five `sltiu` and `cmpi` immediates in each of `music.ap`,
`browser.ap`, and `mainmenu.ap`. `build.sh` sets the limit with `LIB_MAX`, 24,000 by default:
above 28,672 the scan's track-number array would reach its key area, and 24,000 songs
preallocate a 15.9 MB `MUSIC.LIB`. The M3U.LIB scanner and its 4,000-entry limit are
untouched.

## What the player runs showed

- The upgrade installs and the builder runs. The filelist update's progress bar is a step
  counter (`FUN_401d4cb0`, ten notches of 12 px), not a work fraction: 10% after the
  builder's init, 20% when the scan is done, one notch per section of the index passes, one
  after the finalize, then the M3U.LIB build. Only `MUSIC.LIB` and `M3U.LIB` are built by
  `playlist.ap`.
- The scan of 16,000 files takes 10 to 15 minutes and the passes under 10. The first two
  builds stayed at 20% for hours because of the loader entry bug described above; no version
  of the passes had run on the player yet. A later build hung at 20% because piece 3 shared
  the loader's bank window (also above). The `MUSIC.LIB` recreate bug was found from the
  file the first scan left behind.
- The card is exFAT with 32 KB clusters (2,012 directories, 19,938 files), handled by
  `exfatn.drv`, not `fat_32n.drv`. The 8.3 names the stock passes to the delete call are a
  FAT32 fallback. `tools/library/exfat_extents.py` prints how the root files are allocated
  from the raw device: a file created on fragmented free space (`MUSIC.LIB` after the first
  rebuild) has a FAT chain, a contiguous one has the `NoFatChain` flag and no chain.
- The update-needed decision is a flag word in the player's settings flash. `config.ap` reads
  4 bytes at `0x2f0000` through kernel module `0x0f` op `0x12` at boot and launches
  `playlist.ap` (app 10, parameter `0x4e`) when bit 16 is set; it sets the bit itself when the
  settings block at `0x20000` is invalid, which is why the first boot after an upgrade builds
  the library. `playlist.ap` clears the bit only at the very end of a successful update
  (`FUN_48094658` -> `FUN_48094400`). A build that is interrupted therefore restarts on every
  boot until one completes. After a USB disconnect, `udisk.ap` starts the builder directly.
- The player keeps its own view of the card's directories and writes it back: scratch files
  deleted from the PC came back after a player run, and a data file copied to the card was
  not found by one diagnostic run. Unmount cleanly before unplugging, and put data a
  diagnostic build must read inside a file whose directory entry does not change
  (`replay.c` falls back to a copy at 12 MB of `MUSIC.LIB`, written with `dd conv=notrunc`).
- `build.sh --library-bench` makes a diagnostic firmware from `tools/library/bench.c`: it scans
  100 files, then times seven primitive tests (sequential and random 512-byte reads and
  writes, 8 KB reads and writes checked against 512-byte ones, and a second file with its own
  handle), writes the results into song record 0 and the directory walker's entry bytes into
  records 1 and 2, and leaves the library empty. Its run with the string literals missing
  left a hidden 4 MiB file with an empty name in the folder where the scan stopped (create
  with a BOM-only name, attribute 2 from op `0x13`); `fsck.exfat` reports such entries as
  invalid and removing them is safe. Current builds name the file and change to the root
  first.

### The kernel's file I/O bugs

The first whole-card builds (16,096 songs) listed every song in All songs but failed the
comparison with `libmodel.py`: the All songs order was right for its first 768 or 1,365
entries and junk after, the inverse and node tables were junk, and runs of written and
never-written sectors alternated with transitions exactly at 32 KB cluster boundaries. One
library held, in its Album node table, the benchmark's 8 KB fill pattern, which led to a
theory that the kernel's file writes take their data from physical RAM behind the data cache.
That was wrong: the pattern was stale disk data (the benchmark's test 7 had written it into
`MUSIC.LIB` at 1 to 9 MB a reboot earlier, and the passes had never written that sector). The
arena is still used through the uncached alias, which costs nothing measurable. The real
causes came out of three diagnostic firmwares, each a different source in the passes' slot:

- `--library-probe` scans 100 songs, runs the real passes and leaves `MUSIC.TMP` and the
  passes' globals on the card. The library and the scratch file were byte-identical to the
  QEMU harness, but with 100 songs every scratch access stays inside the first cluster.
- `--library-iotest` (`tools/library/iotest.c`) stamps every 512-byte sector it writes with
  the file, the offset and a pass number, and checks every sector it reads, with 8 KB
  sector-aligned calls. Its first run reused a leftover 95 KB `MUSIC.TMP` and failed every
  scratch-file test from the first 32 KB cluster boundary on: the kernel does not extend a
  file past the size it was created with, whatever the write returns. On freshly created
  files of the right size, sequential, random, and out-of-order first writes and reads all
  pass; a file created with size 0 cannot be written at all (T6); and a create on an existing
  name once left two directory entries named `MUSIC.TMP`.
- `--library-replay` (`tools/library/replay.c`, `replay.py`) replays the passes' exact
  sequence of seeks, reads and writes, recorded from a `-DTRACE_OPS` harness run, with
  self-describing data (the word at aligned offset `w` of file `f` is `w ^ KEY[f]`), and
  checks every read whose bytes were all written before. The stream of the sub-sector
  version showed two kernel bugs: a read or write that starts or ends inside a 512-byte
  sector transfers the wrong bytes and leaves the handle's position wrong, and a single call
  that crosses a cluster boundary in a file with a FAT chain stops advancing along the chain
  (the stock code never does either: it moves whole sectors in sector-sized pieces). The
  current passes' stream, 12,676 operations with every call sector-aligned and inside one
  4 KB block, replayed with no errors, and `replay.py synth` makes synthetic streams that
  cross cluster boundaries to confirm the rule.

### Results

The first build of the aligned passes (2026-11-02) produced a 15,991-song library in which
every section's order, inverse, node levels and node index table are consistent with each
other (`selfcheck` in the session notes walks the whole structure) and the Genre section was
identical to the model. The only differences from the model were the placement of names with
characters above U+00FF, which depend on the kernel's text converter: "Phœnix" sorts after
"Phototropic", "D’Angelo" before "DāM‐FunK" before "D12", "JAŸ-Z" after "Jaylib", and
"Tomorrow’s Harvest" after "Tomorrow's Harvest". The model's rule (stop at the first such
character, keep the UTF-16 bytes after it) fits most of them but not all, and the stock
library disagrees with the model even on ASCII ties because of its quicksort, so this
placement is treated as unverifiable rather than wrong. That build also appended junk to
391 of its 515 M4A names, which was piece 3 of the passes stored in the M4A reader's scratch
buffers (see the loader section). The build with the pieces in the `0x48` bank tails
(2026-11-03) is clean: 15,991 songs, 803 artists, 1,711 albums, no junk names, the same
tables as before. The player browses all of it.

The kernel itself is in the firmware image (`kernel.drv`, `ker_*.bin`, `exfatn.drv`,
`fat_32n.drv` and the `drv_*.drv` modules in `build/work/stock/fw/`). Running it under QEMU
with a card image would make the text converter and the file-system behavior observable
without a flash cycle per experiment; nobody has tried yet.

## Test plan

1. Done: `tools/library/harness.c` is a QEMU user-mode harness that loads the stock banks
   `0x400d4c00`, `0x40114c00`, `0x40194c00`, `0x40294c00`, `0x48014400`, and `0x71812500` at
   their real addresses, with every RAM and resident pointer in their literal pools moved by
   `tools/library/testbanks.py` to harness memory (the scan of 4-byte aligned words for pointer
   values matches the decompile's literal-pool list exactly for all six banks), and implements
   the file system ops (two files, `MUSIC.LIB` and `MUSIC.TMP`, each with its own position),
   the text conversions, the append write, and the message pump in C, counting the calls. The
   harness sets up the app state as the scan leaves it, generates the title keys with the stock
   key routine, calls `FUN_400d5290` and the header finalize `FUN_4019502c`, and compares the
   result with the player-built file. `tools/library/test.sh STOCK_PLAYLIST.AP MUSIC.LIB` builds
   and runs it, then `tools/library/libdiff.py` compares the two files section by section.

   Against `notes/MUSIC.LIB.stock-4000` the All songs and Genre sections come out byte for byte
   identical, including the tie refinement, and the Album and Artist sections match except for
   the names with characters above U+00FF described under text conversion (one merged artist
   pair, and three album groups placed differently). The stock passes take about three minutes
   under QEMU at 4,000 songs.

   Two harness details matter for anyone extending it: the key extractor calls the text
   converter with destination 0, so the stock code copies each converted field to address 0 (a
   quirk that is harmless on the player, ignored in the harness), and the file position is
   shared between reads and appends, which the stock code handles by seeking back to the
   append sector after each extraction.
2. Done: `tools/library/libmodel.py` is a Python model of the new passes that computes the
   expected tables and nodes from a library's records, and `test.sh ... new` links the TEST
   build of `index.c` into the harness and compares its output with the model's
   (`INDEX_SRC=... test.sh` runs another source, such as `bench.c`). On the player-built
   sample, on a 4,000-song synthetic library from `tools/library/mklib.py`, and on an
   18,000-song one, every table and node is identical to the model, for the first version and
   for the two-file version. Against the stock passes, `tools/library/libgroups.py` shows the
   only grouping differences are the ASCII-versus-UTF-16 duplicates described above.
3. `build.sh --test` runs the harness on a 6,000-song synthetic library and requires the
   byte-for-byte match.
4. Done: on the player, the benchmark, probe, I/O test and replay firmwares in turn, then the
   real one. A device-built library is checked with `libmodel.py MUSIC.LIB expected.lib` and
   `libdiff.py expected.lib MUSIC.LIB` (every table identical except the non-ASCII names
   above), and `sudo fsck.exfat -n /dev/sda` shows whether a run left junk entries.

## Next: skip the rebuild when the card hasn't changed

The scan is about 0.1 s per file whatever the passes do, so a full rebuild of an 18,000-song
card is around half an hour, and the player starts one after every USB disconnect, even after
charging. The plan is a fingerprint of the directory walk (names, sizes, and times of every
entry) stored in the `MUSIC.LIB` header, computed with a walk only (no file opens) at the
start of the builder, and compared before `FUN_40194c78` recreates the file: if it matches and
the existing library is complete, the music and M3U builds are skipped and the update ends
normally (the flag still gets cleared). What the walker's entry pointer gives access to is
what the diagnostic firmware dumps into records 1 and 2.

## Risks

- The filelist update runs before the menu after every unplug and blocks USB, and an
  interrupted update restarts on every boot. A builder that crashes could stop the player
  from reaching Setting > Auto upgrade, the only known flashing path. In practice a hard
  power-off a few minutes into the update let the menu through once; a marker file on the
  card as a kill switch, or falling back to the stock behavior when the new code fails a
  self-check, is still worth building in.
- The loader can't be tested in QEMU user mode: it depends on the kernel loading a bank when
  a stub in it is called and on reading bank windows as data. Both work on the player, for
  banks in the `0x48` and `0x71` groups. `--no-library` builds a firmware without the patch.
- Every file call is kept inside a 4 KB block, so a card formatted with clusters smaller than
  4 KB would hit the cluster-crossing bug. The player formats cards with 32 KB clusters.
- Names with characters above U+00FF may land in a different place than the PC model
  predicts, and the list screens have been checked by browsing, not exhaustively. The other
  4,000 literals in `music.ap` are an audio sample-rate table and a millisecond timeout, not
  library limits.
