#!/usr/bin/env bash
# Builds the application for native_sim (used by integration and web jobs).
#   fw/scripts/build-native.sh [extra west build args]
source "$(dirname "$0")/common.sh"
need west
cd "$WS_ROOT"
west build -b "$NATIVE_BOARD" -d "$WS_BUILD/native" fw "$@"
echo "native_sim: $WS_BUILD/native/zephyr/zephyr.exe"
