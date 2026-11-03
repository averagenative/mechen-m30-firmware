#!/usr/bin/env bash
# Build patched MECHEN M30 firmware from the official 2025-06-26 MECHEN_M30.HEX.
#
# usage: ./build.sh [--test] [--no-playall] [--no-keys] [--no-library] STOCK_MECHEN_M30.HEX [OUTDIR]
#   --test        also run the QEMU harnesses (shuffle, shuffle all, scroll, keys, library)
#   --no-playall  leave out shuffle-all mode
#   --no-keys     leave out the screen-off media keys
#   --no-library  leave out the whole-card library (keep the stock 4,000-song limit)
#   --library-bench  diagnostic: scan 100 files, then time the file operations (tools/library/bench.c)
#   --library-probe  diagnostic: scan 100 files, run the real passes, leave MUSIC.TMP and the
#                    passes' globals on the card for comparison with the harness
#   --library-iotest diagnostic: scan 100 files, then test the kernel's reads and writes on a
#                    4 MB MUSIC.TMP and the library (tools/library/iotest.c)
#   --library-replay diagnostic: scan 100 files, then replay the passes' file operations from
#                    IOTEST.OPS in the card root with self-describing data (tools/library/replay.c)
# Writes OUTDIR/MECHEN_M30.HEX (default OUTDIR: build). Tools, overridable from the environment:
#   CROSS=mips64-linux-gnu-   ATJBOOTTOOL=atjboottool   QEMU=qemu-mipsel-static   PYTHON=python3
set -euo pipefail

TEST=0 PLAYALL=1 KEYS=1 LIBRARY=1 LIB_MAX=24000 LIB_SRC=index.c LIB_PREALLOC= LIB_DEFS=
while [ $# -gt 0 ]; do
  case $1 in
    --test) TEST=1; shift ;;
    --no-playall) PLAYALL=0; shift ;;
    --no-keys) KEYS=0; shift ;;
    --no-library) LIBRARY=0; shift ;;
    --library-bench) LIB_SRC=bench.c; LIB_PREALLOC=$LIB_MAX; LIB_MAX=100; shift ;;
    --library-probe) LIB_DEFS=-DPROBE; LIB_PREALLOC=$LIB_MAX; LIB_MAX=100; shift ;;
    --library-iotest) LIB_SRC=iotest.c; LIB_PREALLOC=$LIB_MAX; LIB_MAX=100; shift ;;
    --library-replay) LIB_SRC=replay.c; LIB_PREALLOC=$LIB_MAX; LIB_MAX=100; shift ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    *) break ;;
  esac
done
[ $# -ge 1 ] || { sed -n '2,10p' "$0"; exit 2; }
KEY_APPS="browser music mainmenu playlist setting"

ROOT=$(cd "$(dirname "$0")" && pwd); T=$ROOT/tools
STOCK=$(realpath "$1"); OUT=$(realpath -m "${2:-build}"); W=$OUT/work
CROSS=${CROSS:-mips64-linux-gnu-}; ATJ=${ATJBOOTTOOL:-atjboottool}; QEMU=${QEMU:-qemu-mipsel-static}
PY=${PYTHON:-python3}
STOCK_SHA=6c7e27c8c90c4942664d66a0d6c9f9e48f8d3bc7d2d24223568255e233551258
CFLAGS="-EL -mabi=32 -march=mips32 -mips16 -Os -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin
 -nostdlib -fno-toplevel-reorder -fno-jump-tables -fno-asynchronous-unwind-tables -falign-functions=4 -Wall -Wextra"
HFLAGS="-EL -mabi=32 -march=mips32 -mno-mips16 -O1 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -nostdlib"

for t in "${CROSS}gcc" "$ATJ" "$PY"; do command -v "$t" >/dev/null || { echo "missing tool: $t (see README)"; exit 1; }; done
[ $TEST = 0 ] || command -v "$QEMU" >/dev/null || { echo "missing tool: $QEMU (needed for --test)"; exit 1; }
[ "$(sha256sum "$STOCK" | cut -d' ' -f1)" = $STOCK_SHA ] || {
  echo "$STOCK is not the 2025-06-26 MECHEN_M30.HEX (sha256 $STOCK_SHA)."
  echo "The patches use fixed addresses from that release; other versions will fail the stock-byte checks."; exit 1; }

rm -rf "$W"; mkdir -p "$W"/stock/afi "$W"/stock/fw "$W"/verify/afi "$W"/verify/fw
cd "$W"

unpack() {  # HEX DIR: decrypt the FWU, then unpack AFI and FWIMAGE.FW; prints "AFI ok/bad FW ok/bad"
  "$ATJ" -c --fwu -o "$2/dec.bin" "$1" > "$2/fwu.log" 2>&1
  "$ATJ" -c -o "$2/afi/" "$2/dec.bin" > "$2/afi.log" 2>&1
  "$ATJ" -c --fw -o "$2/fw/" "$2/afi/FWIMAGE.FW" > "$2/fw.log" 2>&1
  echo "AFI $(grep -c ' Ok' "$2/afi.log")/$(grep -ci mismatch "$2/afi.log" || true) FW $(grep -c ' Ok' "$2/fw.log")/$(grep -ci mismatch "$2/fw.log" || true)"
}
blob() {  # NAME SOURCE ADDR [CFLAGS...]: compile one MIPS16e unit linked at ADDR -> NAME.bin
  local n=$1 src=$2 at=$3; shift 3
  "${CROSS}gcc" $CFLAGS "$@" -c -o "$n.raw.o" "$src"
  "${CROSS}objcopy" --set-section-alignment .text=4 "$n.raw.o" "$n.o"
  # string literals and const tables (.rodata) go right after the code: the blob is only what
  # the linker puts in .text, and the firmware has nowhere else to load from
  printf 'SECTIONS { . = %s; .text : { *(.text .text.*) *(.rodata .rodata.*) }\n' "$at" > "$n.ld"
  printf ' /DISCARD/ : { *(.MIPS.abiflags) *(.reginfo) *(.pdr) *(.comment) *(.gnu.attributes) *(.mdebug*) } }\n' >> "$n.ld"
  "${CROSS}ld" -EL -m elf32ltsmip -T "$n.ld" -e "$at" -o "$n.elf" "$n.o"
  [ -z "$("${CROSS}size" -A "$n.elf" | awk '$1 ~ /^\.(s?data|s?bss)/ && $2 > 0')" ] || { echo "$n: has writable data, which a blob can't carry"; exit 1; }
  "${CROSS}objcopy" -O binary -j .text "$n.elf" "$n.bin"
}
sym() { "${CROSS}nm" "$1" | awk -v s="$2" '$3==s{print $1}'; }

echo "== unpack stock firmware"
echo "   $(unpack "$STOCK" "$W/stock") (ok/mismatch)"
F=$W/stock/fw

echo "== mengine.ap: shuffle fix$([ $PLAYALL = 1 ] && echo ' + whole-card shuffle')"
blob shuffle "$T/shuffle/shuffle.c" 0x7b1a808c
PA=()
if [ $PLAYALL = 1 ]; then
  blob playall_a "$T/playall/playall_a.c" 0x7b068484
  blob playall_b "$T/playall/playall_b.c" 0x7b1a82ac
  blob playall_c "$T/playall/playall_c.c" 0x7b1a84b4
  blob playall_d "$T/playall/playall_d.c" 0x7b1a8754
  blob playall_e "$T/playall/playall_e.c" 0x7b0a8458
  # the units call each other at the fixed addresses in playall.h: check them against the links
  for c in "a walk WALK" "b mix FOLDER_MIX" "b folder_reseed FOLDER_RESEED" "b folder_next FOLDER_NEXT" \
           "d folder_prev FOLDER_PREV" "d folder_song FOLDER_SONG"; do
    set -- $c
    want=$(awk -v n="$3" '$1 == "#define" && $2 == n { sub(/u$/, "", $3); print $3; exit }' "$T/playall/playall.h")
    got=$(sym playall_$1.elf $2)
    [ $((0x$got + 1)) = $((want)) ] || { echo "playall.h has $3 $want but $2 links at 0x$got"; exit 1; }
  done
  PA=(playall_a.bin playall_b.bin playall_c.bin playall_d.bin playall_e.bin)
fi
"$PY" -I "$T/patch_mengine.py" "$F/mengine.ap" mengine.ap shuffle.bin "${PA[@]}" | sed 's/^/   /'

echo "== browser.ap, music.ap: scroll acceleration"
"${CROSS}gcc" $CFLAGS -c -o tramp.raw.o "$T/scroll/tramp.S"
"${CROSS}objcopy" --set-section-alignment .text=4 tramp.raw.o tramp.o
for spec in "browser 0xbfc16a25u" "music 0xbfc16bb1u" "test"; do
  set -- $spec; n=$1
  if [ $n = test ]; then def="-DTEST -DTEST_TIMER=0x00700000u -DTEST_RAM=0x10000100u"; else def="-DTIMER_STUB=$2"; fi
  "${CROSS}gcc" $CFLAGS $def -c -o accel_$n.raw.o "$T/scroll/accel.c"
  "${CROSS}objcopy" --set-section-alignment .text=4 accel_$n.raw.o accel_$n.o
  "${CROSS}ld" -EL -m elf32ltsmip -Ttext=0x48714a80 -e stock_next -o accel_$n.elf tramp.o accel_$n.o
  "${CROSS}objcopy" -O binary -j .text accel_$n.elf accel_$n.bin
done
NX=$(sym accel_browser.elf accel_next); PV=$(sym accel_browser.elf accel_prev)
for n in music test; do
  [ "$(sym accel_$n.elf accel_next)" = "$NX" ] && [ "$(sym accel_$n.elf accel_prev)" = "$PV" ] || { echo "scroll blob layout differs for $n"; exit 1; }
done
"$PY" -I "$T/scroll/patch_scroll.py" "$F/browser.ap" 0x18900 accel_browser.bin browser.ap "$NX" "$PV"
"$PY" -I "$T/scroll/patch_scroll.py" "$F/music.ap" 0x2d200 accel_music.bin music.ap "$NX" "$PV"
echo "   blob $(stat -c %s accel_browser.bin) bytes at 0x48714a80, accel_next 0x$NX, accel_prev 0x$PV"
PATCHED="mengine.ap browser.ap music.ap"

if [ $KEYS = 1 ]; then
  echo "== $KEY_APPS: screen-off media keys"
  "${CROSS}gcc" $CFLAGS -c -o keypass.raw.o "$T/keys/keypass.S"
  "${CROSS}objcopy" --set-section-alignment .text=4 keypass.raw.o keypass.o
  "${CROSS}ld" -EL -m elf32ltsmip -Ttext=0x48694b80 -e keypass -o keypass.elf keypass.o
  "${CROSS}objcopy" -O binary -j .text keypass.elf keypass.bin
  for app in $KEY_APPS; do
    [ -f $app.ap ] || cp "$F/$app.ap" $app.ap        # browser and music already carry the scroll patch
    "$PY" -I "$T/keys/patch_keys.py" $app.ap keypass.bin $app.ap
    case " $PATCHED " in *" $app.ap "*) ;; *) PATCHED="$PATCHED $app.ap" ;; esac
  done
  echo "   hook 0x486947d0 -> keypass at 0x48694b80 in $KEY_APPS"
fi

if [ $LIBRARY = 1 ]; then
  echo "== playlist.ap: whole-card library (up to $LIB_MAX songs$([ $LIB_SRC != index.c ] && echo ", DIAGNOSTIC BUILD: $LIB_SRC")$([ -n "$LIB_DEFS" ] && echo ", DIAGNOSTIC PROBE BUILD"))"
  blob libindex "$T/library/$LIB_SRC" 0xbfc3c000 $LIB_DEFS
  blob libload "$T/library/loader.c" 0x400d5290
  # the stock caller jumps to the first byte of each blob: the entry function must be first
  [ "$(sym libindex.elf index_passes)" = "bfc3c000" ] || { echo "index_passes is not at the start of the passes blob"; exit 1; }
  [ "$(sym libload.elf load_and_run)" = "400d5290" ] || { echo "load_and_run is not at the start of the loader"; exit 1; }
  [ -f playlist.ap ] || cp "$F/playlist.ap" playlist.ap
  "$PY" -I "$T/library/patch_library.py" playlist.ap libload.bin libindex.bin playlist.ap $LIB_MAX $LIB_PREALLOC | sed 's/^/   /'
  for app in music browser mainmenu; do
    [ -f $app.ap ] || cp "$F/$app.ap" $app.ap
    "$PY" -I "$T/library/patch_library.py" --reader $app.ap $app.ap $LIB_MAX | sed 's/^/   /'
  done
  case " $PATCHED " in *" playlist.ap "*) ;; *) PATCHED="$PATCHED playlist.ap" ;; esac
  case " $PATCHED " in *" mainmenu.ap "*) ;; *) PATCHED="$PATCHED mainmenu.ap" ;; esac
fi

echo "== repack"
"$PY" -I "$T/repack_fw.py" "$STOCK" "$W/stock/dec.bin" "$OUT/MECHEN_M30.HEX" \
  $(for f in $PATCHED; do printf '%s=%s ' $f $f; done) | tail -n 1 | sed 's/^/   /'

echo "== verify"
echo "   $(unpack "$OUT/MECHEN_M30.HEX" "$W/verify") (ok/mismatch)"
"$PY" -I -c '
import struct, sys
d = open(sys.argv[1], "rb").read(); n = struct.unpack_from("<I", d, 16)[0]
ok = sum(struct.unpack("<%dI" % (n // 4), d[:n])) & 0xffffffff == struct.unpack_from("<I", d, n + 0x10)[0]
print("   FwuTail", "ok" if ok else "MISMATCH"); sys.exit(not ok)' "$OUT/MECHEN_M30.HEX"
for f in $PATCHED; do cmp -s $f "$W/verify/fw/$f" || { echo "   extracted $f differs from the patched input"; exit 1; }; done
changed=$(for f in "$F"/*; do b=$(basename "$f"); cmp -s "$f" "$W/verify/fw/$b" || printf '%s ' "$b"; done)
echo "   files changed from stock: $changed"
[ "$changed" = "$(printf '%s\n' $PATCHED | sort | tr '\n' ' ')" ] || { echo "   unexpected set of changed files"; exit 1; }
"$PY" -I "$T/refscan.py" mengine.ap 0x7300:0x7b068000:0x800 0x7b00:0x7b0a8000:0x500 0x8d00:0x7b1a8000:0x800 > refscan.txt

if [ $TEST = 1 ]; then
  echo "== QEMU tests"
  "${CROSS}gcc" $HFLAGS -c -o harness_sh.o "$T/shuffle/harness.c"
  blob shuffle_test "$T/shuffle/shuffle.c" 0x7b1a808c -DTEST -DTEST_RAM=0x10000000u -DTEST_RAND=0x00500000u
  mkdir -p t_shuffle && cp harness_sh.o t_shuffle/harness.o && cp shuffle_test.o t_shuffle/
  (cd t_shuffle && "${CROSS}ld" -EL -m elf32ltsmip -T "$T/shuffle/test.ld" -o test.elf harness.o shuffle_test.o)
  timeout 600 "$QEMU" -cpu 24Kc t_shuffle/test.elf > test_shuffle.txt || true
  echo "   shuffle:  $(tail -n 1 test_shuffle.txt)"

  if [ $PLAYALL = 1 ]; then
    TD="-DTEST -DTEST_RAM=0x10000000u -DTEST_RAND=0x00500000u -DTEST_VFS=0x00505000u -DTEST_RESET_ROOT=0x00501000u
      -DTEST_COUNT_FILES=0x00502000u -DTEST_SEEK_FILE=0x00503000u -DTEST_NEXT_CROSS=0x00504000u -DTEST_WALK=0x7b068485u
      -DTEST_FOLDER_RESEED=0x7b1a82d5u -DTEST_FOLDER_NEXT=0x7b1a836du -DTEST_FOLDER_MIX=0x7b1a82adu
      -DTEST_FOLDER_PREV=0x7b1a8755u -DTEST_FOLDER_SONG=0x7b1a878du -DTEST_PREV_CROSS=0x00506000u"
    mkdir -p t_playall; cd t_playall
    for u in a b c d e; do
      "${CROSS}gcc" $CFLAGS $TD -c -o ${u}_test_raw.o "$T/playall/playall_$u.c"
      "${CROSS}objcopy" --set-section-alignment .text=4 ${u}_test_raw.o ${u}_test.o
    done
    "${CROSS}gcc" $HFLAGS -Wno-unused-function -c -o harness.o "$T/playall/harness.c"
    "${CROSS}ld" -EL -m elf32ltsmip -T "$T/playall/test.ld" -o test.elf harness.o a_test.o b_test.o c_test.o d_test.o e_test.o
    cd ..
    timeout 900 "$QEMU" -cpu 24Kc t_playall/test.elf > test_playall.txt || true
    echo "   playall:  $(tail -n 1 test_playall.txt)"
  fi

  mkdir -p t_scroll; cd t_scroll
  "$PY" -I "$T/scroll/testbank.py" "$F/browser.ap" ../accel_test.bin "$NX" "$PV" .
  "${CROSS}gcc" $HFLAGS -c -o harness.o "$T/scroll/harness.c" 2>&1 | grep -v 'mixed declarations' || true
  sed 's/bank.o(.data)/bank.o(.bankcode)/' "$T/scroll/test.ld" > t.ld
  for v in stock patched; do
    printf '\t.section .bankcode,"awx"\n\t.incbin "%s"\n' "$PWD/bank_${v}_test.bin" > bank.s
    "${CROSS}as" -EL -mabi=32 -mips32 -o bank.o bank.s
    for slow in 1 0; do
      "${CROSS}ld" -EL -m elf32ltsmip -T t.ld --defsym SLOW_ONLY=$slow -o run_${v}_$slow.elf harness.o bank.o
      timeout 600 "$QEMU" -cpu 24Kc ./run_${v}_$slow.elf > out_${v}_$slow.txt || true
    done
  done
  cd ..
  echo "   scroll:   patched, mixed clicks: $(tail -n 1 t_scroll/out_patched_0.txt)"
  if cmp -s <(tail -n +2 t_scroll/out_stock_1.txt) <(tail -n +2 t_scroll/out_patched_1.txt); then
    echo "   scroll:   slow clicks identical to stock"
  else
    echo "   scroll:   SLOW CLICKS DIFFER FROM STOCK"
  fi
  if [ $KEYS = 1 ]; then
    mkdir -p t_keys; cd t_keys
    "${CROSS}gcc" $HFLAGS -c -o harness.o "$T/keys/harness.c"
    for app in $KEY_APPS; do
      "$PY" -I "$T/keys/patch_keys.py" --testbanks "$F/$app.ap" ../keypass.bin .
      printf '\t.section .bankrun,"awx"\n\t.globl bank_run\nbank_run:\t.space 0x600\n\t.section .rodata\n\t.globl bank_stock, bank_patched\nbank_stock:\t.incbin "%s"\nbank_patched:\t.incbin "%s"\n' \
        "$PWD/bank_stock_test.bin" "$PWD/bank_patched_test.bin" > banks.s
      "${CROSS}as" -EL -mabi=32 -mips32 -o banks.o banks.s
      "${CROSS}ld" -EL -m elf32ltsmip -T "$T/keys/test.ld" -o test_$app.elf harness.o banks.o
      timeout 300 "$QEMU" -cpu 24Kc ./test_$app.elf > ../test_keys_$app.txt || true
      echo "   keys:     $app: $(tail -n 2 ../test_keys_$app.txt | tr '\n' ' ')"
    done
    cd ..
  fi
  if [ $LIBRARY = 1 ]; then
    mkdir -p t_library
    "$PY" -I "$T/library/mklib.py" 6000 t_library/lib6k.lib > /dev/null
    CROSS=$CROSS QEMU=$QEMU PYTHON=$PY "$T/library/test.sh" "$F/playlist.ap" t_library/lib6k.lib t_library new > test_library.txt 2>&1 || true
    echo "   library:  $(grep -m1 -E 'reproduced|MISMATCH' test_library.txt || echo 'no result')"
  fi
  for f in test_*.txt; do grep -q '^ALL PASS' $f || { echo "   $f: no ALL PASS line"; echo FAIL >> missing_pass.txt; }; done
  for f in t_scroll/out_*.txt; do grep -q '^events 30000 .* failures 0$' $f || { echo "   $f: incomplete"; echo FAIL >> missing_pass.txt; }; done
  fails=$(cat test_*.txt t_scroll/out_patched_*.txt missing_pass.txt 2>/dev/null | grep -c -E 'FAIL|MISMATCH|INVARIANT|FLAGS' || true)
  slow_same=$(cmp -s <(tail -n +2 t_scroll/out_stock_1.txt) <(tail -n +2 t_scroll/out_patched_1.txt) && echo 1 || echo 0)
  [ "$fails" = 0 ] && [ "$slow_same" = 1 ] || { echo "   TESTS FAILED (logs in $W)"; exit 1; }
fi

echo "== done: $OUT/MECHEN_M30.HEX"
sha256sum "$OUT/MECHEN_M30.HEX"
