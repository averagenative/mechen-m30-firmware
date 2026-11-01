#!/usr/bin/env bash
# Run the stock MUSIC.LIB index passes in QEMU against a library the player built, and check
# that they reproduce its index tables.
#
# usage: tools/library/test.sh STOCK_PLAYLIST.AP MUSIC.LIB [WORKDIR]
# Tools, overridable from the environment, as for build.sh:
#   CROSS=mips64-linux-gnu-   QEMU=qemu-mipsel-static   PYTHON=python3
set -euo pipefail
[ $# -ge 2 ] || { sed -n '2,8p' "$0"; exit 2; }
T=$(cd "$(dirname "$0")" && pwd)
AP=$(realpath "$1"); LIB=$(realpath "$2"); W=$(realpath -m "${3:-build/t_library}")
CROSS=${CROSS:-mips64-linux-gnu-}; QEMU=${QEMU:-qemu-mipsel-static}; PY=${PYTHON:-python3}
HFLAGS="-EL -mabi=32 -march=mips32 -mno-mips16 -O1 -G0 -mno-abicalls -fno-pic -ffreestanding -fno-builtin -nostdlib"
mkdir -p "$W"; cd "$W"
"${CROSS}gcc" $HFLAGS -Wall -Wextra -c -o harness.o "$T/harness.c"
"$PY" -I "$T/testbanks.py" "$AP" .
{
  for b in 400d4c00 40114c00 40194c00 40294c00 48014400 71812500; do
    printf '\t.section .bank_%s,"awx"\n\t.incbin "%s/bank_%s.bin"\n' $b "$PWD" $b
  done
  printf '\t.section .expect,"a"\n\t.globl expect_image, expect_len\nexpect_image:\t.incbin "%s"\n\t.balign 4\nexpect_len:\t.word expect_len - expect_image\n' "$LIB"
} > banks.s
"${CROSS}as" -EL -mabi=32 -mips32 -o banks.o banks.s
"${CROSS}ld" -EL -m elf32ltsmip -T "$T/test.ld" -o test.elf harness.o banks.o
timeout 900 "$QEMU" -cpu 24Kc ./test.elf | tee harness.txt || true
"$PY" -I "$T/libdiff.py" "$LIB" harness_out.lib | tee libdiff.txt
