#!/usr/bin/env bash
set -euo pipefail

cmake_file="${1:-CMakeLists.txt}"

version="$(sed -nE 's/^[[:space:]]*project\([^)]*VERSION[[:space:]]+([^[:space:])]+).*/\1/p' "$cmake_file" | head -n 1)"
if [[ -z "$version" ]]; then
    printf 'error: could not find project VERSION in %s\n' "$cmake_file" >&2
    exit 1
fi

printf '%s\n' "$version"
