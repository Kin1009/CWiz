#!/bin/sh
# sync_vendored_cwmode.sh - keep the cwmode core identical in vendored Wiz trees.
#
# The emulator links the Wiz compiler in-process (third_party/wiz).  The Add
# Modes panel uses the same cwmode library as the standalone driver, so the
# vendored copies must be byte-identical:
#
#   compiler/src/cwmode.{hpp,cpp}
#   compiler/src/third_party/stb_image.h
#
# The stable snapshot is deliberately excluded: this tool only updates the
# preview trees.
#
# Usage: tools/sync_vendored_cwmode.sh [--check]
#   --check   only report differences, do not copy

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
src="$root/compiler/src"
check=0
[ "${1:-}" = "--check" ] && check=1

targets="
${WIZ_VENDORED_DIR:-}
$root/emulator/CasioEmuMsvc/third_party/wiz/src
$root/../CasioEmuMsvc-Mac/CasioEmuMsvc/third_party/wiz/src
"

status=0
for dir in $targets; do
    [ -d "$dir" ] || continue
    for f in cwmode.cpp cwmode.hpp; do
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
    if [ ! -f "$dir/third_party/stb_image.h" ]; then
        if [ "$check" = "1" ]; then
            echo "missing: $dir/third_party/stb_image.h"
            status=1
        else
            mkdir -p "$dir/third_party"
            cp "$src/third_party/stb_image.h" "$dir/third_party/stb_image.h"
            echo "updated $dir/third_party/stb_image.h"
        fi
    elif cmp -s "$src/third_party/stb_image.h" "$dir/third_party/stb_image.h"; then
        echo "ok      $dir/third_party/stb_image.h"
    elif [ "$check" = "1" ]; then
        echo "DIFFERS $dir/third_party/stb_image.h"
        status=1
    else
        cp "$src/third_party/stb_image.h" "$dir/third_party/stb_image.h"
        echo "updated $dir/third_party/stb_image.h"
    fi
done

exit $status
