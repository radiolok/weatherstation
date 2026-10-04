#!/usr/bin/env bash
# CI job "web": Node tests of the browser renderer (same golden frames as C)
# and Playwright against the web server of the native_sim application.
source "$(dirname "$0")/common.sh"
need node
cd "$WS_ROOT"
node --test fw/web/test/*.test.js
"$WS_FW/scripts/build-native.sh"
mkdir -p "$WS_BUILD/web"
python3 -m pytest fw/tests/web -v \
	--zephyr-exe "$WS_BUILD/native/zephyr/zephyr.exe" \
	--log-dir "$WS_BUILD/web" "$@"
