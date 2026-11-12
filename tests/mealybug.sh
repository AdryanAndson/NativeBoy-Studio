#!/bin/sh
# Compares the PPU output with photos-derived reference images from mealybug-tearoom-tests
# (mid-scanline register changes). Prints the share of matching pixels per test.
N=$(cd "$(dirname "$0")/.." && pwd)
ROMS=$1; EXP=$2
tot=0; exact=0; sum=0
for rom in "$ROMS"/*.gb; do
  name=$(basename "$rom" .gb)
  ref="$EXP/DMG-blob/$name.png"; [ -f "$ref" ] || ref="$EXP/DMG-CPU B/$name.png"; [ -f "$ref" ] || continue
  "$N/build/host/nbtest" "$rom" 30 --gray --ppm /tmp/mb_$name.ppm > /dev/null 2>&1
  convert /tmp/mb_$name.ppm /tmp/mb_$name.png
  diff=$(compare -metric AE "$ref" /tmp/mb_$name.png null: 2>&1 | cut -d' ' -f1)
  pct=$(python3 -c "print(round(100*(1-$diff/23040),1))")
  tot=$((tot+1)); [ "$diff" = 0 ] && exact=$((exact+1))
  sum=$(python3 -c "print($sum+$pct)")
  echo "$name: $pct% ($diff px diferentes)"
done
echo "TOTAL: $exact/$tot idênticos; média $(python3 -c "print(round($sum/$tot,1))")% dos pixels corretos"
