# MECHEN M30 custom firmware: shuffle fix, shuffle all songs, fast scrolling, and screen-off buttons

Unofficial firmware patches for the **MECHEN M30 HiFi MP3 player**. They fix the broken shuffle on large music libraries, add a shuffle-all-songs mode to the folder view, add scroll-wheel acceleration so you can move through thousands of folders quickly, and make the play, track, and volume buttons work while the screen is off. The patches apply to the official MECHEN M30 firmware release dated 2025-06-26 (`MECHEN_M30.HEX`) and install through the player's normal firmware update. You can download the patched firmware from a [release](#download-a-patched-release), [make it from the stock file](#make-the-patched-firmware-from-the-stock-file) with one Python command, or [build it from source](#build-the-firmware).

The MECHEN M30 is built on an Actions Semiconductor ATJ2127-family chip (USB ID `10d6:1101`). This repository also includes tools to decrypt, unpack, modify, and repack Actions ATJ2127 FWU firmware files, which can help with reverse engineering similar Actions-based MP3 players.

> **Warning:** This is unofficial firmware built from reverse engineering. A bad flash can leave your player unable to boot. Before you install it, read [Recover a player](#recover-a-player). You use it at your own risk.

## What the patches fix

The patches change four things in the stock MECHEN M30 firmware:

- **Shuffle fix.** On a large SD card, stock shuffle plays the same small part of the library, mostly artists that start with A, B, or C. The patch replaces the stock shuffle with one that reaches every track on the card.
- **Shuffle all songs from folder view.** Stock firmware only shuffles inside the current folder. With **Repeat all** and **Shuffle** on, the patched player shuffles across every folder on the card, and prev steps back through the songs it already played.
- **Scroll-wheel acceleration.** Stock firmware moves one item per wheel click. The patched folder view speeds up while you keep turning the wheel: 1, then 5, 20, 50, and 100 items per click. A pause of 0.7 seconds or a change of direction drops back to 1, and fast scrolling stops at the top and bottom of the list instead of wrapping.
- **Buttons work with the screen off.** With stock firmware, after the backlight timer turns the screen off, the first press of any button only turns the screen back on, and the press is lost. With the patch, the play/pause, prev, next, volume up, and volume down buttons act immediately and the screen stays off. The wheel, OK, and back buttons still turn the screen on, and the power button still turns it on or off.

## Supported players and firmware

The patches work only with this combination:

- **Player:** MECHEN M30 with the Actions firmware line. MECHEN publishes these releases as date-named folders (for example 2022-08-16 and 2025-06-26) that contain `MECHEN_M30.HEX` and a `.fw` file.
- **Firmware:** the 2025-06-26 `MECHEN_M30.HEX`, SHA-256 `6c7e27c8c90c4942664d66a0d6c9f9e48f8d3bc7d2d24223568255e233551258`. MECHEN distributes it from a [public Google Drive folder](https://drive.google.com/drive/folders/1Y9v8c6FLR5lbxLY-PhAv-BQOeQa2BNev), in the `2025-06-26` folder ([direct link](https://drive.google.com/file/d/1lTyyP05toQHbcnmmiK3R9FFToj9RZfxC/view)).

Other firmware versions don't work, because the patches use fixed code addresses. The build checks the stock bytes at every patch site and stops if they don't match. The "Version 2.0" release (`SDM30008.img`) is for a different board with a Rockchip RKnano chip, and these patches don't apply to it.

The shuffle fix, shuffle-all mode, scroll acceleration, and screen-off buttons run on a MECHEN M30 with a 256 GB exFAT card that holds about 18,000 tracks.

## Known limitations

These issues are still present in the patched firmware:

- The first song you play in shuffle-all mode starts after a short delay while the player walks the card's folder tree.
- In shuffle-all mode, prev goes back only as far as the first song that shuffle-all chose in the current pass over the card. From there, prev works as in stock firmware and picks another song in the same folder. After the player has played every folder once, it starts a new random order, and prev can't go back past that point.
- With the screen off, the buttons act on whichever screen was open when it turned off. On the now-playing screen, prev and next change tracks. In the folder view, they move the hidden selection instead, the same as they do with the screen on.
- The **All songs** library view still stops at 4,000 tracks. The library builder in the stock firmware sets that limit. The folder view and shuffle-all mode aren't limited.

## Download a patched release

Each release on the [releases page](https://github.com/averagenative/mechen-m30-firmware/releases) has two files:

- `MECHEN_M30.HEX`: the patched firmware, ready to [install](#install-the-firmware-on-the-mechen-m30).
- `MECHEN_M30-vN.bps`: a small patch that turns the stock 2025-06-26 firmware into the same patched file. The [`patches`](patches) folder of this repository holds the same patch files.

## Make the patched firmware from the stock file

Instead of downloading the patched firmware, you can make it from MECHEN's stock firmware with one Python command. The patch holds only the changes, and it checks the stock file before it writes anything. You need Git and Python 3.

1. Clone this repository:

   ```
   git clone https://github.com/averagenative/mechen-m30-firmware.git
   ```

1. Go to the repository folder:

   ```
   cd mechen-m30-firmware
   ```

1. Download the stock 2025-06-26 [`MECHEN_M30.HEX`](https://drive.google.com/file/d/1lTyyP05toQHbcnmmiK3R9FFToj9RZfxC/view) from MECHEN's Google Drive, and save it in the repository folder. To download it from the command line instead, run:

   ```
   curl -L -o MECHEN_M30.HEX 'https://drive.google.com/uc?export=download&id=1lTyyP05toQHbcnmmiK3R9FFToj9RZfxC'
   ```

1. Apply the patch for the version you want. For example, for v5:

   ```
   python3 tools/bps.py apply MECHEN_M30.HEX patches/MECHEN_M30-v5.bps build/MECHEN_M30.HEX
   ```

   The command writes `build/MECHEN_M30.HEX` and prints its SHA-256, which matches the one in the release notes. If the stock file isn't the 2025-06-26 release, the command stops with an error and writes nothing.

1. [Install the firmware](#install-the-firmware-on-the-mechen-m30) from `build/MECHEN_M30.HEX`.

To apply a patch without Git or Python, open [Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/) in a web browser. Select the stock `MECHEN_M30.HEX` as the ROM file and the `.bps` file as the patch, click **Apply patch**, and then rename the result to `MECHEN_M30.HEX`. [Floating IPS](https://github.com/Alcaro/Flips) on Windows or Linux also works.

## Build the firmware

The repository doesn't contain any MECHEN firmware. You download the stock file and build the patched one on your own computer.

### Requirements

You need the following:

- The stock 2025-06-26 `MECHEN_M30.HEX` described in [Supported players and firmware](#supported-players-and-firmware).
- A MIPS cross compiler with MIPS16e support. The build was tested with Fedora's `gcc-mips64-linux-gnu` 16.2.1 and `binutils-mips64-linux-gnu` 2.46.1. To use a different toolchain prefix, set `CROSS`.
- `atjboottool` from the [Rockbox source tree](https://github.com/Rockbox/rockbox), in `utils/atj2137/atjboottool`. It decrypts and unpacks Actions firmware. The build uses it to unpack the stock file and to check the output.
- Python 3.
- For the tests only: `qemu-mipsel-static` (Fedora package `qemu-user-static-mips`).

To install the requirements on Fedora, run:

```sh
sudo dnf install gcc-mips64-linux-gnu binutils-mips64-linux-gnu qemu-user-static-mips zlib-devel
git clone --depth 1 --filter=blob:none https://github.com/Rockbox/rockbox.git
make -C rockbox/utils/atj2137/atjboottool
```

### Run the build

Run the build script with the path to the stock file:

```sh
ATJBOOTTOOL=rockbox/utils/atj2137/atjboottool/atjboottool ./build.sh --test path/to/MECHEN_M30.HEX
```

The script writes `build/MECHEN_M30.HEX` and keeps intermediate files in `build/work/`. It runs these steps:

1. Decrypts and unpacks the stock firmware.
1. Compiles the patch code to MIPS16e and patches `mengine.ap`, `browser.ap`, `music.ap`, `mainmenu.ap`, `playlist.ap`, and `setting.ap`.
1. Repacks the firmware with `tools/repack_fw.py`, which recomputes every checksum and re-encrypts the file.
1. Unpacks the result again and checks every checksum, the trailer sum, and that exactly the patched files differ from stock.
1. With `--test`, runs the QEMU test harnesses on an emulated MIPS 24Kc CPU. The scroll and screen-off buttons tests run the real stock code from the firmware, stock and patched side by side.

The script takes these options:

- `--test`: runs the QEMU tests. They take a few minutes.
- `--no-playall`: leaves out shuffle-all mode.
- `--no-keys`: leaves out the screen-off buttons patch.

With the tested toolchain, the full build is byte-identical to the v5 release, SHA-256 `af999b7bf15e4a6412e7fc119bff3e59a4a3cfd2079bcf0e47b9662bdd40936c`, which runs on the test player. To build an earlier release, check out its tag (`v3`, `v4`) first. A different compiler version can produce a different but valid file.

## Install the firmware on the MECHEN M30

The patched firmware installs the same way as an official MECHEN M30 firmware update:

1. Connect the player to your computer over USB.
1. If the card already has a `MECHEN_M30.HEX` or `MECHEN_M30~.HEX` in its root folder, delete it. The player only updates when exactly one firmware file is present.
1. Copy the patched `MECHEN_M30.HEX`, from a release or from `build/`, to the root folder of the card.
1. Eject the player and unplug it.
1. Wait for the player to finish "filelist update". On a large card, it takes several minutes.
1. In the main menu, go to **Setting** > **Auto upgrade**.
1. When the player shows "Select to upgrade with[MECHEN_M30.HEX]", confirm it.
1. Leave the player on while it shows "Please do not power off during firmware upgrade", until it shows "firmware upgrade succeed".

After the upgrade, the player renames the file to `MECHEN_M30~.HEX`, so it doesn't install it again.

### Turn on shuffle all songs

To shuffle every song on the card:

1. Start playing any song.
1. On the now-playing screen, open the menu and select **Play mode**.
1. Set **Repeat** to **Repeat all** and **Shuffle** to on.
1. Go to the folder view and play any song.

## Recover a player

If a patched build stops the player from booting, try these options in order:

1. If the player still reads the card, flash the stock `MECHEN_M30.HEX` the same way you installed the patched one.
1. Use the MECHEN Windows upgrade tool with the stock `.fw` file. The chip's mask ROM has a USB recovery mode (ADFU). It shows up as USB ID `10d6:10d6` when you hold a boot key while you plug the player in. The boot key for the M30 isn't known.
1. On Linux, try [actions_flash](https://github.com/ilyakurdyukov/actions_flash). It supports the ATJ2127 but isn't complete.

## Frequently asked questions

### Why does MECHEN M30 shuffle keep playing the same artists?

The stock shuffle stores the number of 64-track rows in a single byte. Above 16,383 tracks the count wraps around, so shuffle only reaches the first part of the library, which on most cards means artists starting with A to C. The stock picker also gives up after three random tries and falls back to scanning in order. The shuffle fix in this repository replaces it with a Feistel permutation that plays every track once before it repeats.

### Can the MECHEN M30 shuffle all songs on the SD card?

Not with stock firmware: shuffle in the folder view stays inside one folder, and the **All songs** view stops at 4,000 tracks. With the patched firmware, **Repeat all** plus **Shuffle** in the folder view shuffles across every folder on the card, with no track limit. Prev and next move backward and forward through the same random order, so prev returns to the song you just heard.

### How do I scroll faster through folders on the MECHEN M30?

Install the patched firmware and keep turning the wheel. After about one second of steady turning, each click moves 5 items. After 2.5 seconds it moves 20, after 4 seconds 50, and after 6 seconds 100.

### Why don't the MECHEN M30 buttons work when the screen is off?

In stock firmware, every app passes button presses through a common key filter. While the screen is off, the filter turns any press except power into a "wake the screen" message and discards the press. The wake handler then records which button woke the screen, and the filter also discards that button's long-press and release events, so the press does nothing except turn the screen on. The patch lets play/pause, prev, next, and volume through to the app with the screen left off.

### Why does the MECHEN M30 library stop at 4,000 songs?

The stock library builder (`playlist.ap`) holds its sort tables in a fixed 120 KB RAM region that fits 4,000 tracks. These patches don't change that limit. Use the folder view, which isn't limited. `docs/library-builder.md` explains how the builder works and what a whole-card library would take.

### Do these tools work with other Actions ATJ2127 MP3 players?

The decrypt and repack tools (`tools/repack_fw.py`, `tools/fwu_repack.py`) handle the generic Actions ATJ2127 FWU, AFI, and FWIMAGE.FW formats, so they may work with firmware for other ATJ2127 players. The patches themselves only match the MECHEN M30 2025-06-26 release.

## Repository layout

The repository contains these files and folders:

| Path | Contents |
|---|---|
| `build.sh` | Builds and checks a patched `MECHEN_M30.HEX` from the stock file. |
| `patches/` | The BPS patch for each release. `tools/bps.py` applies one to the stock `MECHEN_M30.HEX`. |
| `tools/shuffle/` | Shuffle fix (`shuffle.c`) and its QEMU harness. |
| `tools/playall/` | Shuffle-all mode: folder walker (A), folder permutation (B), next-track hook (C), step back and song choice (D), and prev-track hook (E), plus the harness. |
| `tools/scroll/` | Scroll acceleration (`accel.c`, `tramp.S`), the app patcher, and the harness. |
| `tools/keys/` | Screen-off buttons (`keypass.S`), the app patcher, and the harness. |
| `tools/library/` | Library builder work in progress: a QEMU harness that runs the stock MUSIC.LIB index passes (`harness.c`, `testbanks.py`, `test.sh`) and `libdiff.py` to compare two libraries. See `docs/library-builder.md`. |
| `tools/patch_mengine.py` | Applies the shuffle fix and shuffle-all code to `mengine.ap`. |
| `tools/bps.py` | Creates and applies the BPS patches that releases use. |
| `tools/repack_fw.py`, `tools/fwu_repack.py` | Rebuild the FWU, AFI, and FWIMAGE.FW layers with new checksums and encryption. |
| `tools/refscan.py` | Lists every stock function inside a patched range and every call or pointer to it. Run it on any new patch so you don't overwrite live code. |
| `tools/apinfo.py`, `tools/hiddenbanks.py`, `tools/findbase.py` | Read `.ap` headers and bank tables, and find the load addresses of banks that aren't in the table. |
| `tools/apcalls.py`, `tools/kcalls.py`, `tools/findconst.py`, `tools/findimm.py` | Search Ghidra output and firmware files for system calls, constants, and immediates. |
| `tools/ghidra/` | Ghidra headless scripts: `LoadAP.java` maps an `.ap` file's banks, and `DumpC.java` writes the decompiled C. |
| `tools/musiclib.py` | Decodes the player's `MUSIC.LIB` library database. |
| `tools/shufsim.py` | Simulates the stock shuffle algorithm to measure its bias. |

## Technical notes

These notes describe the parts of the firmware that the patches depend on. All addresses are for the 2025-06-26 release.

### Actions ATJ2127 firmware format

`MECHEN_M30.HEX` is an Actions FWU v3 file. It contains an AFI container, which contains `FWIMAGE.FW`, which contains the apps (`.ap` files with MIPS code), drivers (`.drv`), UI resources (`.sty`), menu trees (`.mcg`), and fonts. The payload cipher is a per-512-byte-sector XOR keystream. `repack_fw.py` recovers the session key from the stock file by known plaintext, so it can re-encrypt without the vendor tool. Every checksum is an additive sum, and the FWU trailer is the 32-bit word sum of the file up to `fw_size`. The firmware has no signatures. A no-op repack reproduces the stock file byte for byte.

### App overlays and banks

An `.ap` file has a header with the resident code, a data segment, an entry point, and a bank table at `0x100` of `{file offset, load address, size}` entries. The resident segment is described by `{file offset, size, load address}` at header offset `0x08`; in most apps that is file offset `0xc00` loaded at `0xbfc16400`, but `playlist.ap` has two resident segments (the triple at `0x14` puts file `0xc00` at `0xbfc11d00`, and the main one is at file `0x1400`). Code is MIPS16e. Banks load on demand: a call into a bank that isn't loaded raises an exception, and the kernel loads the bank and retries the call. The table holds 12-byte entries in blocks of 64 per bank group (five groups, `0x100` to `0x1000`; the length word at `0x2c` understates it), and the kernel loads exactly the listed size of a bank. The music engine's group `0x7b` sits at `0x700`, and the shared-library group `0x71` at `0xa00`, past the part of the table that `apinfo.py` reads. It has bank 1 at file `0x7300` (loaded at `0x7b068000`), bank 2 at file `0x7b00` (loaded at `0x7b0a8000`), and bank 6 at file `0x8d00` (loaded at `0x7b1a8000`).

System calls are stubs at the end of the resident code, `li v1,MODULE; break`, with the function ID as the last argument. The patches use module `0x0f` (kernel: function `0x1c` is the millisecond clock, `0x35` is the random number generator) and module `0x11` (file system).

### Patch sites

| Patch | File | Location |
|---|---|---|
| Shuffle fix | `mengine.ap` | Code over the stock `init_shuffle` at `0x7b1a808c`. The stock picker entry `0x7b1a84b0` becomes a branch to the new `shuffle_next`. |
| Shuffle all songs | `mengine.ap` | A over a dead disk-counting routine at `0x7b068484`, which must end before the live `get_location` at `0x7b06856c`. B at `0x7b1a82ac`. C in the dead picker body at `0x7b1a84b4`. The Repeat All next-track call at `0x7b068088` becomes `jal 0x7b1a84b4`. D in the zero tail of bank 6 at `0x7b1a8754`, and E in the zero tail of bank 2 at `0x7b0a8458`. Both Repeat All prev-track calls (`0x7b0a80de`, `0x7b0a812e`) become `jal 0x7b0a8458`. The bank table entries at `0x718` and `0x748` of `mengine.ap` get larger load sizes (`0x4d8` and `0x7b8`) to cover D and E. |
| Scroll acceleration | `browser.ap` (file `0x18900`), `music.ap` (file `0x2d200`) | Folder-list bank `0x48714400`. The stock next and prev entries at `0x487144a8` and `0x487146cc` branch to code in the free bank tail at `0x48714a80`. State uses 12 bytes of unused RAM at `0x9fc15bf4`. |
| Screen-off buttons | `browser.ap`, `music.ap`, `mainmenu.ap`, `playlist.ap`, `setting.ap` | Common key filter bank `0x48694600`. The first instruction of the filter's screen-off wake path, `0x486947d0`, branches to 22 bytes in the free bank tail at `0x48694b80`. That code sends keys `01`, `02`, `03`, `05`, and `06` to the filter's screen-on path and every other key back to the stock wake path. |

### Keys

Key map records are 6 bytes: `{u8 key, u8 0, u16 type, u16 message}`. Key types are `0x2000` (down), `0x1000` (long press), `0x0800` (hold), and `0x0400` (short release). The key codes are:

| Code | Key |
|---|---|
| `01` | Play/pause |
| `02`, `03` | Prev, next |
| `05`, `06` | Volume up, volume down |
| `07` | OK |
| `0a`, `0b` | Wheel |
| `80` | Back |
| `81` | Power |

Each app filters keys through a common function, `FUN_48694658` in bank `0x48694600`. The system state at `0x9fc1a1c8` holds the screen state at `+0x18` (0 on, 2 off) and the key that woke the screen at `+3`. With the screen off, stock firmware turns any key other than power into message `0x106` (wake) and discards it. The `0x106` handler records the key at `+3`, and the filter discards that key's later events until it's released. The screen-off message `0x105` turns off the backlight and LCD and sets scene `0x89`, which pauses the playing screen's refresh timers. Waking requests a full redraw.

## License

The code in this repository is released under the [MIT License](LICENSE). MECHEN M30 firmware is the property of its owners and isn't included. This project isn't affiliated with MECHEN or Actions Semiconductor.
