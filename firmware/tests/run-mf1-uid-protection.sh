#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname -- "$0")/../.."
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
# Nordic includes are unused in this host harness; declarations live in the
# forced platform header. All generated files stay outside the source tree.
for header in netdata.h fds_util.h nrf_log.h nrf_log_ctrl.h nrf_log_default_backends.h; do
    touch "$build_dir/$header"
done
# Crypto1 uses the ARM byte-swap intrinsic in its PRNG helper.
printf '#define __REV(value) __builtin_bswap32(value)\n' > "$build_dir/cmsis_gcc.h"
rfid=firmware/application/src/rfid
"${CC:-cc}" -std=c11 -O1 -g -ffunction-sections -fdata-sections \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$build_dir" -I "$rfid" -I "$rfid/nfctag" -I "$rfid/nfctag/hf" \
    -include firmware/tests/mf1_host_platform.h \
    firmware/tests/test_mf1_uid_protection.c \
    "$rfid/mf1_crapto1.c" "$rfid/parity.c" "$rfid/hex_utils.c" \
    "$rfid/nfctag/hf/crypto1_helper.c" \
    -Wl,--gc-sections -o "$build_dir/test-mf1"
"$build_dir/test-mf1"
