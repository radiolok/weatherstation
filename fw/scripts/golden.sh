#!/usr/bin/env bash
# CI job "golden": regenerate reference frames with Node (core.js and the web
# renderer) and compare them with the C renderer on native_sim.
source "$(dirname "$0")/common.sh"
need node
cd "$WS_ROOT"
node tools/glyphgen/glyphgen.js
node tools/glyphgen/golden.js
node fw/web/tools/golden-screens.js
# Fails if the committed references differ from what core.js draws now.
git diff --exit-code -- fw/lib/sign/glyphs_gen.h fw/web/src/glyphs.json fw/tests/golden
west twister -p "$NATIVE_BOARD" -T fw/tests/lib --tag golden \
	--outdir "$WS_BUILD/twister-golden" --inline-logs
