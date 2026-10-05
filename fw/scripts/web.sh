#!/usr/bin/env bash
# CI job "web": Node tests of the browser renderer (same golden frames as C),
# Playwright against the mock server (host build of fw/lib), then Playwright
# against the web server of the native_sim application.
#   fw/scripts/web.sh            all three
#   fw/scripts/web.sh --mock     without Zephyr (node + mock backend)
source "$(dirname "$0")/common.sh"
need node
cd "$WS_ROOT"
mock_only=0
if [[ "${1:-}" == "--mock" ]]; then
	mock_only=1
	shift
fi
node --test fw/web/test/*.test.js
"$WS_FW/scripts/host-tests.sh" >/dev/null
mkdir -p "$WS_BUILD/web"
python3 -m pytest fw/tests/web -v --web-backend=mock --wsapi "$WS_BUILD/host/wsapi" "$@"
if [[ $mock_only == 1 ]]; then
	exit 0
fi
"$WS_FW/scripts/build-native.sh"
python3 -m pytest fw/tests/web -v --web-backend=device \
	--zephyr-exe "$WS_BUILD/native/zephyr/zephyr.exe" \
	--log-dir "$WS_BUILD/web" "$@"
