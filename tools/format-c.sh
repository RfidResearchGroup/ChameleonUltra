#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

case "${1:-}" in
  "") args=(-i) ;;
  --check) args=(--dry-run --Werror --ferror-limit=1) ;;
  *) echo "Usage: $0 [--check]" >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo "Usage: $0 [--check]" >&2
  exit 2
fi

find firmware software/src -type f \( -name '*.c' -o -name '*.h' \) -print0 |
  xargs -0 uvx --from clang-format==22.1.8 clang-format --style=file "${args[@]}"
