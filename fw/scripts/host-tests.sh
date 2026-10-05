#!/usr/bin/env bash
# Fast loop: fw/lib ztest suites built for the host with the ztest shim
# (fw/tests/host), ASan/UBSan on. Does not need Zephyr or west.
#   fw/scripts/host-tests.sh [--coverage] [suite-filter]
source "$(dirname "$0")/common.sh"
need cmake
need ninja

cov=OFF
if [[ "${1:-}" == "--coverage" ]]; then
	cov=ON
	shift
fi
dir="$WS_BUILD/host"
cmake -S "$WS_FW/tests/host" -B "$dir" -G Ninja -DWS_COVERAGE=$cov -DWS_SANITIZE=$([[ $cov == ON ]] && echo OFF || echo ON) >/dev/null
ninja -C "$dir"
ctest --test-dir "$dir" --output-on-failure ${1:+-R "$1"}
if [[ $cov == ON ]] && command -v gcovr >/dev/null; then
	gcovr -r "$WS_FW/lib" "$dir" --print-summary --html-details -o "$dir/coverage.html"
fi
