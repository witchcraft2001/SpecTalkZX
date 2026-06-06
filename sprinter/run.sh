#!/bin/sh
# run.sh — boot the Sprinter MAME emulator with this project's floppy mounted.
# Builds distr/sptalk.img first (via make deploy), then launches MAME with it
# as flop2. At the DSS prompt, switch to the floppy drive and run SPTALK.

set -e

PROJ_DIR="$(cd "$(dirname "$0")" && pwd)"
MAME_DIR="/Users/dmitry/dev/zx/sprinter/mame_images/mame_release_v306_25.05.2025"
IMG="$PROJ_DIR/distr/sptalk.img"

make -C "$PROJ_DIR" deploy

cd "$MAME_DIR"
./mame sprinter \
  -skip_gameinfo \
  -video opengl \
  -window \
  -nofilter \
  -beta:wd179x:0 525qd \
  -beta:wd179x:1 35hd \
  -flop2 "$IMG" \
  -isa0 zxbus_adapter \
  -isa0:zxbus_adapter:card neogs \
  -hard1 ./IMG/sp_hdd_sys.chd \
  -hard2 ./IMG/sp_hdd_media.chd \
  -ata2:0 cdrom -cdrom ./IMG/SprinterCD.iso \
  -bios v3.06
