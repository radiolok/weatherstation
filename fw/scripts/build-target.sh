#!/usr/bin/env bash
# CI job "target": ESP32-S3 with sysbuild + MCUboot, signed image, size report.
#   fw/scripts/build-target.sh [extra west build args]
source "$(dirname "$0")/common.sh"
need west
cd "$WS_ROOT"
out="$WS_BUILD/esp32s3"
west build -b "$TARGET_BOARD" --sysbuild -d "$out" fw "$@"

art="$WS_BUILD/artifacts"
mkdir -p "$art"
app="$out/fw/zephyr"
cp "$app/zephyr.signed.bin" "$art/weatherstation-ota.signed.bin"
cp "$app/zephyr.elf" "$art/weatherstation.elf"
cp "$out/mcuboot/zephyr/zephyr.bin" "$art/mcuboot.bin"
# One file for the first flashing over USB (offsets: boards/esp32s3_ws_partitions.dtsi)
python3 "$WS_FW/scripts/merge_bin.py" --out "$art/weatherstation-merged.bin" \
	0x0:"$art/mcuboot.bin" 0x20000:"$art/weatherstation-ota.signed.bin"
west build -d "$out/fw" -t rom_report >"$art/rom_report.txt" 2>&1 || true
west build -d "$out/fw" -t ram_report >"$art/ram_report.txt" 2>&1 || true
python3 "$WS_FW/scripts/size.py" --elf "$app/zephyr.elf" --budget "$WS_FW/size-budget.json" \
	--json "$art/size.json"
sha256sum "$art"/*.bin | tee "$art/SHA256SUMS"
