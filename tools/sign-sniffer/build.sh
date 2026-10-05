#!/usr/bin/env bash
# Builds the sniffer into build/sign-sniffer/sign-sniffer (repository root).
set -euo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
build=${WS_BUILD:-$root/build}/sign-sniffer
gen=$(command -v ninja >/dev/null && echo Ninja || echo "Unix Makefiles")
cmake -S "$root/tools/sign-sniffer" -B "$build" -G "$gen" >/dev/null
cmake --build "$build"
echo "$build/sign-sniffer"
