#!/bin/sh
# build.sh - configure and build the Wiz / CWiz toolchain into ./build.
#
#     ./build.sh              release build
#     ./build.sh --debug      debug build
#     ./build.sh --clean      wipe ./build first
#
# After a successful build the tools are:
#     build/wizc  build/wizrun  build/customromrun  build/patch_menu  build/wiztests
set -e

cd "$(dirname "$0")"

BUILD_TYPE=Release
CLEAN=0
EXTRA=""
for arg in "$@"; do
    case "$arg" in
        --debug) BUILD_TYPE=Debug ;;
        --clean) CLEAN=1 ;;
        *) EXTRA="$EXTRA $arg" ;;
    esac
done

if [ "$CLEAN" = 1 ]; then
    rm -rf build
fi

GEN=""
if command -v ninja >/dev/null 2>&1; then
    GEN="-G Ninja"
fi

# shellcheck disable=SC2086
cmake -S compiler -B build $GEN -DCMAKE_BUILD_TYPE="$BUILD_TYPE" $EXTRA
cmake --build build --parallel

echo
echo "built:"
for t in wizc wizrun customromrun patch_menu wiztests; do
    [ -x "build/$t" ] && echo "  build/$t"
done
