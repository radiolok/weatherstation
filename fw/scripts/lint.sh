#!/usr/bin/env bash
# CI job "lint": clang-format and generated files are up to date.
#   fw/scripts/lint.sh [--fix]
source "$(dirname "$0")/common.sh"
need clang-format
need node
need python3

fix=0
[[ "${1:-}" == "--fix" ]] && fix=1

cd "$WS_ROOT"
mapfile -t files < <(ws_c_sources)
if ((fix)); then
	clang-format -i "${files[@]}"
else
	clang-format --dry-run --Werror "${files[@]}"
fi

# Glyph tables, golden frames and legacy protocol vectors come from
# tools/sign-simulator/core.js and legacy/rpi/scripts/futaba.py.
flag=--check
((fix)) && flag=
node tools/glyphgen/glyphgen.js $flag
node tools/glyphgen/golden.js $flag
node fw/web/tools/golden-screens.js $flag
python3 tools/glyphgen/futaba_vectors.py $flag
echo "lint: OK"
