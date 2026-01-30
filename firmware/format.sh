#!/bin/env bash

# Make it a bit less prone to breaking
cd "$(dirname "$0")/.." || exit

while IFS= read -r -d '' f; do
    clang-format --verbose -i "$f"
done < <(find . -type f \( -name '*.c' -o -name '*.h' \) ! -path './firmware/nrf52_sdk/*' ! -path './firmware/nrf52_sdk/**' -print0)