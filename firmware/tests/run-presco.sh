#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="$(mktemp -d)"
trap 'rm -rf "$build_dir"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -g \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I "$repo_root/firmware/tests/include" \
    -I "$repo_root/firmware/application/src/rfid/nfctag" \
    -I "$repo_root/firmware/application/src/rfid/nfctag/lf/protocols" \
    "$repo_root/firmware/application/src/rfid/nfctag/lf/protocols/presco.c" \
    "$repo_root/firmware/tests/test_presco.c" -o "$build_dir/test-presco"
"$build_dir/test-presco"
