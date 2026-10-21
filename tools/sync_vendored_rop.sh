#!/bin/sh
# sync_vendored_rop.sh - copy the ROP backend into the vendored emulator trees.
#
# The CasioEmuMsvc app vendors the Wiz compiler (third_party/wiz).  The ROP
# backend lives in compiler/src/rop.{cpp,hpp} and compiler/src/rop_data.hpp;
# the vendored copies must stay byte-identical, otherwise the app compiles
# ROP payloads differently from the standalone wizc driver.
#
# Usage: tools/sync_vendored_rop.sh [--check]
#   --check   only report differences, do not copy

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/compiler/src"
check=0
[ "${1:-}" = "--check" ] && check=1

targets="
$root/emulator/CasioEmuMsvc/third_party/wiz/src
$root/../CasioEmuMsvc-Mac-stable/CasioEmuMsvc/third_party/wiz/src
"

status=0
for dir in $targets; do
    [ -d "$dir" ] || continue
    for f in rop.cpp rop.hpp rop_data.hpp; do
        if [ ! -f "$dir/$f" ]; then
            echo "missing: $dir/$f"
            status=1
            continue
        fi
        if cmp -s "$src/$f" "$dir/$f"; then
            echo "ok      $dir/$f"
        elif [ "$check" = "1" ]; then
            echo "DIFFERS $dir/$f"
            status=1
        else
            cp "$src/$f" "$dir/$f"
            echo "updated $dir/$f"
        fi
    done
done

exit $status
