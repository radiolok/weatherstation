#!/usr/bin/env bash
# CI job "unit": all ztest suites on native_sim with twister and gcov.
#   fw/scripts/unit.sh [extra twister args]
source "$(dirname "$0")/common.sh"
need west
cd "$WS_ROOT"
west twister -p "$NATIVE_BOARD" -T fw/tests/lib \
	--coverage --coverage-basedir fw --coverage-tool gcovr \
	--outdir "$WS_BUILD/twister-unit" --inline-logs "$@"
