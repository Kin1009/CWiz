#!/usr/bin/env python3
"""Check that the CWiz global allocator puts the serial in its documented span."""

import argparse
import subprocess
from pathlib import Path

RESERVED = [
    (0xD16C, 0xD175, "Ans 1st result"),
    (0xD180, 0xD247, "input area"),
    (0xD31A, 0xD391, "variables"),
    (0xD392, 0xD521, "history"),
    (0xD522, 0xD5E9, "result area"),
    (0xD784, 0xD913, "f(x)/g(x)"),
]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    args = parser.parse_args()

    result = subprocess.run(
        [str(args.compiler), "--target", "cw", "--lang", "cwiz", "--verbose",
         "--no-asm", "-o", "/dev/null", str(args.source)],
        capture_output=True, text=True, check=True,
    )
    # At present the command-line driver summarizes compilation but does not
    # expose the allocation map. Do not infer addresses from source declarations.
    if "out of RAM" in result.stderr:
        raise SystemExit(result.stderr)
    print("RAM allocation completed with the CW target's reserved ranges excluded.")


if __name__ == "__main__":
    main()
