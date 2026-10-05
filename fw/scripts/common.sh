# Shared helpers for fw/scripts/*.sh (sourced).
set -euo pipefail
WS_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
WS_FW="$WS_ROOT/fw"
WS_BUILD=${WS_BUILD:-$WS_ROOT/build}
NATIVE_BOARD=native_sim/native/64
TARGET_BOARD=esp32s3_devkitc/esp32s3/procpu

need() {
	command -v "$1" >/dev/null 2>&1 || { echo "error: '$1' not found" >&2; exit 2; }
}

# Source files checked by clang-format (generated files excluded).
ws_c_sources() {
	cd "$WS_ROOT"
	git ls-files -co --exclude-standard -- 'fw/lib/*.[ch]' 'fw/src/*.[ch]' 'fw/src/*.cpp' \
		'fw/emul/*.[ch]' 'fw/tests/*.[ch]' |
		grep -v -e '_gen\.h$' -e '/golden/' || true
}
