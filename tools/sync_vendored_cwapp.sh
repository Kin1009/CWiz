#!/bin/sh
# sync_vendored_cwapp.sh - keep the cwapp / cwapplink compiler core in sync.
#
# The emulator links the Wiz compiler in-process (third_party/wiz).  The
# .cwapplink builder and the extra #cwapp form changed these core files, so the
# vendored copies must be byte-identical:
#
#   build.hpp build.cpp cpre.hpp cpre.cpp targets.cpp patch.hpp patch.cpp
#   cwlink.hpp cwlink.cpp cwmode.hpp cwmode.cpp asset_fallback.hpp
#   data/runtime.asm data/template_*.asm third_party/stb_image.h
#
# The stable snapshot is deliberately excluded.
#
# Usage: tools/sync_vendored_cwapp.sh [--check]

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

files="build.hpp build.cpp cpre.hpp cpre.cpp targets.cpp patch.hpp patch.cpp cwlink.hpp cwlink.cpp cwmode.hpp cwmode.cpp asset_fallback.hpp"
data_files="runtime.asm template_ML620906.asm template_ML620909.asm template_cwapp.asm template_fwnx.asm"
status=0
for dir in $targets; do
    [ -d "$dir" ] || continue
    for f in $files; do
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
    data_dir=$(dirname "$dir")/data
    for f in $data_files; do
        if [ ! -f "$data_dir/$f" ]; then
            echo "missing: $data_dir/$f"
            status=1
            continue
        fi
        if cmp -s "$root/compiler/$f" "$data_dir/$f"; then
            echo "ok      $data_dir/$f"
        elif [ "$check" = "1" ]; then
            echo "DIFFERS $data_dir/$f"
            status=1
        else
            cp "$root/compiler/$f" "$data_dir/$f"
            echo "updated $data_dir/$f"
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
