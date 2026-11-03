#!/usr/bin/env python3
"""Verify the CWiz allocator's RAM map for the fx-580 (cw) target.

wizc does not emit a separate map file, but the assembly listing records every
absolute address the allocator assigned to a global, local, temporary or array
element.  This tool compiles the program with `-S`, parses those addresses, and
checks them against the target's RAM window and the firmware spans the CW
target must not use (compiler/src/targets.cpp, TARGET_CW).
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

# Keep in sync with compiler/src/targets.cpp: TARGET_CW.
VAR_BASE = 0xD000
VAR_LIMIT = 0xDDD3  # first byte of the shadow framebuffer SB2
RESERVED = [
    (0xD16C, 0xD175, "Ans 1st result"),
    (0xD180, 0xD247, "input area"),
    (0xD31A, 0xD391, "variables"),
    (0xD392, 0xD521, "history"),
    (0xD522, 0xD5E9, "result area"),
    (0xD784, 0xD913, "f(x)/g(x)"),
]

# `L ER0, 53334` / `ST R0, 53249`: the numeric operand is an absolute address
# assigned by the allocator.  ERn accesses are words, Rn accesses are bytes.
MEMORY_ACCESS = re.compile(r"\b(L|ST|LD|STD)\s+(ER\d+|R\d+),\s*(\d+)\b")


def parse_allocations(listing: Path):
    refs = []
    for line_number, line in enumerate(listing.read_text(encoding="utf-8").splitlines(), 1):
        code = line.split(";", 1)[0]
        for match in MEMORY_ACCESS.finditer(code):
            address = int(match.group(3))
            if not VAR_BASE <= address < 0x10000:
                continue
            width = 2 if match.group(2).startswith("ER") else 1
            refs.append((address, address + width, line_number, line.strip()))
    return refs


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--listing", type=Path)
    parser.add_argument("--keep", action="store_true", help="keep the generated listing")
    args = parser.parse_args()

    listing = args.listing or (args.source.resolve().parent / "build" / "verify-ram.asm")
    listing.parent.mkdir(parents=True, exist_ok=True)
    try:
        result = subprocess.run(
            [str(args.compiler), "--target", "cw", "--lang", "cwiz", "-S",
             "-o", str(listing), str(args.source)],
            capture_output=True, text=True,
        )
        if result.returncode != 0:
            sys.stderr.write(result.stdout)
            sys.stderr.write(result.stderr)
            raise SystemExit("wizc failed; cannot verify the RAM layout")
        if not listing.exists():
            raise SystemExit(f"wizc did not write an assembly listing: {listing}")

        refs = parse_allocations(listing)
    finally:
        if not args.keep:
            listing.unlink(missing_ok=True)

    if not refs:
        raise SystemExit("no allocated RAM addresses found in the assembly listing")

    highest = max(end for _, end, _, _ in refs)
    lowest = min(start for start, _, _, _ in refs)
    errors = []

    if highest > VAR_LIMIT:
        errors.append(
            f"allocator overruns the RAM window: highest address 0x{highest - 1:04X} "
            f"(end 0x{highest:04X}) passes VAR_LIMIT 0x{VAR_LIMIT:04X}"
        )

    for start, end, line_number, text in sorted(refs):
        for reserved_start, reserved_end, name in RESERVED:
            if start <= reserved_end and end - 1 >= reserved_start:
                errors.append(
                    f"line {line_number}: allocation 0x{start:04X}..0x{end - 1:04X} "
                    f"overlaps reserved {name} (0x{reserved_start:04X}..0x{reserved_end:04X})\n"
                    f"    {text}"
                )

    if errors:
        raise SystemExit("RAM layout verification failed:\n" + "\n".join(errors))

    print(
        f"RAM layout OK: {len(refs)} allocated references, "
        f"lowest 0x{lowest:04X}, highest 0x{highest - 1:04X} "
        f"(window 0x{VAR_BASE:04X}..0x{VAR_LIMIT - 1:04X}, "
        f"{VAR_LIMIT - highest} bytes free); no reserved-span overlap"
    )


if __name__ == "__main__":
    main()
