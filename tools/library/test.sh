#!/usr/bin/env bash
# Run the stock MUSIC.LIB index passes in QEMU against a library the player built, and check
# that they reproduce its index tables.
#
# usage: tools/library/test.sh STOCK_PLAYLIST.AP MUSIC.LIB [WORKDIR] [stock|new|both]
# "new" (the default) runs the file-backed passes from index.c and compares their output with
# what libmodel.py computes from the same records; "stock" runs the passes from the stock
# bank and compares with MUSIC.LIB itself (so MUSIC.LIB must be a player-built library);
# "both" runs both.  MUSIC.LIB can also come from mklib.py.
# Tools, overridable from the environment, as for build.sh:
#   CROSS=mips64-linux-gnu-   QEMU=qemu-mipsel-static   PYTHON=python3
# INDEX_SRC=tools/library/replay.c OPS=IOTEST.OPS runs the op replay instead of the passes.
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,8p' "$0"; exit 2; }
T=$(cd "$(dirname "$0")" && pwd)
AP=$(realpath "$1"); LIB=$(realpath "$2"); W=$(realpath -m "${3:-build/t_library}"); MODE=${4:-new}
CROSS=${CROSS:-mips64-linux-gnu-}; QEMU=${QEMU:-qemu-mipsel-static}; PY=${PYTHON:-python3}
HFLAGS="-EL -mabi=32 -march=mips32 -mno-mips16 -O1 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -nostdlib"
CFLAGS="-EL -mabi=32 -march=mips32 -mips16 -Os -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin
 -nostdlib -fno-toplevel-reorder -fno-jump-tables -fno-asynchronous-unwind-tables -falign-functions=4 -Wall -Wextra"
mkdir -p "$W"; cd "$W"
"${CROSS}gcc" $HFLAGS ${HEXTRA:-} -Wall -Wextra -c -o harness.o "$T/harness.c"
"${CROSS}gcc" $CFLAGS -DTEST -I"$T" -c -o index_test.raw.o "${INDEX_SRC:-$T/index.c}"
"${CROSS}objcopy" --set-section-alignment .text=4 index_test.raw.o index_test.o
echo "index.c: $("${CROSS}size" -A index_test.o | awk '$1==".text"{print $2}') bytes of MIPS16"
"$PY" -I "$T/testbanks.py" "$AP" .
{
  for b in 400d4c00 40114c00 40194c00 40294c00 48014400 71812500; do
    printf '\t.section .bank_%s,"awx"\n\t.incbin "%s/bank_%s.bin"\n' $b "$PWD" $b
  done
  if [ -n "${OPS:-}" ]; then printf '\t.section .ops,"a"\n\t.globl ops_image, ops_len\nops_image:\t.incbin "%s"\n\t.balign 4\nops_len:\t.word ops_len - ops_image\n' "$(realpath "$OPS")"
  else printf '\t.section .ops,"a"\n\t.globl ops_image, ops_len\nops_image:\nops_len:\t.word 0\n'; fi
  printf '\t.section .expect,"a"\n\t.globl expect_image, expect_len\nexpect_image:\t.incbin "%s"\n\t.balign 4\nexpect_len:\t.word expect_len - expect_image\n' "$LIB"
} > banks.s
"${CROSS}as" -EL -mabi=32 -mips32 -o banks.o banks.s
"$PY" -I "$T/libmodel.py" "$LIB" expected.lib
for v in stock new; do
  [ "$MODE" = both ] || [ "$MODE" = $v ] || continue
  if [ $v = new ]; then ref=$PWD/expected.lib; flag=1; else ref=$LIB; flag=0; fi
  sed "s|expect_image:\t.incbin \".*\"|expect_image:\t.incbin \"$ref\"|" banks.s > banks_$v.s
  "${CROSS}as" -EL -mabi=32 -mips32 -o banks_$v.o banks_$v.s
  "${CROSS}ld" -EL -m elf32ltsmip -T "$T/test.ld" --defsym use_new=$flag -o test_$v.elf harness.o banks_$v.o index_test.o
  timeout 1800 "$QEMU" -cpu 24Kc ./test_$v.elf | tee harness_$v.txt || true
  mv harness_out.lib out_$v.lib
  "$PY" -I "$T/libdiff.py" "$ref" out_$v.lib | tee libdiff_$v.txt
done
