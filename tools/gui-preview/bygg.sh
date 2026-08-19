#!/bin/sh
# Bygger host-forhandsvisningen av enhetens skarmar och renderar PPM-bilder.
# Kraver lvgl i Arduino-biblioteksmappen (samma kalla som firmware bygger mot)
# och vanliga gcc/g++. C-filerna byggs med gcc och C++-filerna med g++ - att
# blanda sprak i ett anrop later sig inte goras palitligt.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
SKETCH="$(cd "$HERE/../../firmware/Hikaya" && pwd)"
LVGL="${LVGL_DIR:-$HOME/Arduino/libraries/lvgl}"

test -d "$LVGL/src" || { echo "hittar inte lvgl i $LVGL" >&2; exit 1; }

OUT="$HERE/out"
mkdir -p "$OUT/obj"
cd "$OUT"

CFLAGS="-O0 -g -w -DLV_CONF_PATH=\"$SKETCH/lv_conf.h\" -I$LVGL -I$LVGL/.. -I$SKETCH -I$HERE"

# C-filerna: lvgl och typsnitten. Objekten aterbrukas mellan korningar -
# lvgl andras inte, sa andra korningen ar sekundsnabb.
find "$LVGL/src" -name '*.c' > cfiles.txt
for f in "$SKETCH"/ui_font_*.c; do echo "$f" >> cfiles.txt; done
while read -r f; do
  o="obj/$(echo "$f" | tr '/.' '__').o"
  [ "$o" -nt "$f" ] && continue
  gcc $CFLAGS -c "$f" -o "$o" &
  while [ "$(jobs | wc -l)" -ge 8 ]; do wait -n 2>/dev/null || wait; done
done < cfiles.txt
wait

g++ $CFLAGS -std=gnu++17 -c "$SKETCH/gui_screens.cpp" -o obj/gui_screens.o
g++ $CFLAGS -std=gnu++17 -c "$HERE/preview.cpp" -o obj/preview.o

g++ obj/*.o -o preview -lm
./preview
echo "bilder i $OUT"
