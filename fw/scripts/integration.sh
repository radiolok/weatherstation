#!/usr/bin/env bash
# CI job "integration": native_sim application + pytest against a local
# Mosquitto (MQTT_HOST, default 127.0.0.1:1883), HTTPS and NTP test servers.
source "$(dirname "$0")/common.sh"
need west
"$WS_FW/scripts/build-native.sh"
mkdir -p "$WS_BUILD/integration"
cd "$WS_ROOT"
python3 -m pytest fw/tests/integration -v \
	--zephyr-exe "$WS_BUILD/native/zephyr/zephyr.exe" \
	--log-dir "$WS_BUILD/integration" "$@"
