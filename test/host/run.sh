#!/bin/bash
# Pruebas de PC del nucleo portable de TenoRSS (no hace falta el equipo).
#   test/host/run.sh [directorio con feeds .xml para probar el parser]
set -e
cd "$(dirname "$0")/../.."
OUT=${TMPDIR:-/tmp}/tenorss-hosttest
mkdir -p "$OUT"
FLAGS="-std=c++11 -O1 -g -Wall -Wextra -fsanitize=address,undefined"
g++ $FLAGS -o "$OUT/unittests" test/host/unittests.cpp src/core/*.cpp
"$OUT/unittests" "$OUT/cache_sample.json"
python3 -c "import json,sys; json.load(open(sys.argv[1], encoding='utf-8')); print('cache de ejemplo: JSON valido')" "$OUT/cache_sample.json"
g++ $FLAGS -o "$OUT/feedtool" test/host/feedtool.cpp src/core/feedparser.cpp src/core/textutil.cpp src/core/datetime.cpp
if [ -n "$1" ]; then
  for f in "$1"/*.xml; do
    printf "%-16s " "$(basename "$f" .xml)"
    "$OUT/feedtool" "$f" | tail -1
  done
fi
