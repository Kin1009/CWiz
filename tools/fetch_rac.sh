#!/bin/sh
# fetch_rac.sh - fetch the RAC-Compiler backend required by #rop sources.
#
# RAC-Compiler is an external GPL-3.0 program.  It is NOT linked into the Wiz
# compiler binary; wizc only invokes `rac.py` as a separate process.  #rop
# sources cannot be compiled without it, but ordinary Wiz/CWiz programs (and the
# rest of the toolchain) work without Python or RAC.
#
# The clone lives in tools/rac-compiler/ (gitignored).  Point wizc at another
# checkout with $RAC_COMPILER or --rac PATH.
#
# Usage: tools/fetch_rac.sh [--update]
#   --update   pull the latest revision into an existing clone

set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
dest="${RAC_DIR:-$root/tools/rac-compiler}"
repo="${RAC_REPO:-https://github.com/luongvantam/RAC-Compiler.git}"

if [ ! -d "$dest/.git" ]; then
    if [ -d "$dest" ] && [ -n "$(ls -A "$dest" 2>/dev/null)" ]; then
        echo "error: $dest exists and is not a RAC-Compiler clone" >&2
        exit 1
    fi
    echo "cloning RAC-Compiler (GPL-3.0) into $dest"
    git clone --depth 1 "$repo" "$dest"
else
    if [ "${1:-}" = "--update" ]; then
        echo "updating $dest"
        git -C "$dest" pull --ff-only
    else
        echo "already present: $dest (use --update to refresh)"
    fi
fi

if [ ! -f "$dest/rac.py" ]; then
    echo "error: $dest/rac.py is missing; is this the RAC-Compiler repository?" >&2
    exit 1
fi

cat <<EOF

RAC-Compiler is ready: $dest/rac.py

  wizc examples/rop/01_hello.cwiz
  wizc --emit-rsc examples/rop/01_hello.cwiz -o hello.rsc
  (cd "$dest" && python3 rac.py 580vnx /absolute/path/hello.rsc)

The ROP pipeline needs Python 3.  Set RAC_COMPILER to skip --rac, or point
wizc --rac at another checkout.
EOF
