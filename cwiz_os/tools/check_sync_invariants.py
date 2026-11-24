#!/usr/bin/env python3
"""Check the canonical cwapp RAM-map invariants (C-RAM-45 M2).

Read-only, offline gate for the 13-entry cwapp exclusion map.  It verifies the
byte-budget invariants (5113 + 3 + 7172 = 12288, conflicts 1551, safe 5621)
and checks parity between:

  * the canonical table in ``verify_cwapp_exclusions.py`` (the cwapp exclusion
    verifier's single source of truth), and
  * ``compiler/src/targets.cpp::TARGET_CWAPP.reserved_ram`` (the compiler map).

Exit status: 0 when every check passes, 1 otherwise.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))

import verify_cwapp_exclusions as vx  # noqa: E402

CHECKS = 0
FAILS: list[str] = []


def check(name: str, cond: bool, extra: str = "") -> None:
    global CHECKS
    CHECKS += 1
    if cond:
        print("PASS " + name)
    else:
        print("FAIL " + name + (("  " + extra) if extra else ""))
        FAILS.append(name)


def canonical_invariants() -> None:
    forbidden = sum(hi - lo + 1 for lo, hi, _ in vx.FORBIDDEN_RANGES)
    immutable = sum(hi - lo + 1 for lo, hi, _ in vx.IMMUTABLE)
    space = 0x10000 - vx.RAM_LOW
    allowed = space - forbidden - immutable
    conflicts = sum(hi - lo + 1 for lo, hi, _ in vx.GUIDE_UNMAPPED)
    safe = allowed - conflicts

    check("RAM: 8 forbidden ranges", len(vx.FORBIDDEN_RANGES) == 8)
    check("RAM: 3 immutable addresses", len(vx.IMMUTABLE) == 3)
    check("RAM: 2 conservative guide gaps", len(vx.GUIDE_UNMAPPED) == 2)
    check("RAM: 13-entry exclusion table", len(vx.EXCLUSIONS) == 13)
    check("RAM: forbidden bytes == 5113", forbidden == 5113, str(forbidden))
    check("RAM: immutable bytes == 3", immutable == 3, str(immutable))
    check("RAM: allowed bytes == 7172", allowed == 7172, str(allowed))
    check("RAM: space bytes == 12288", space == 12288, str(space))
    check("RAM: 5113 + 3 + 7172 == 12288",
          forbidden + immutable + allowed == space == 12288)
    check("RAM: conflict bytes == 1551", conflicts == 1551, str(conflicts))
    check("RAM: safe bytes == 5621", safe == 5621, str(safe))
    check("RAM: immutables are F0C3/F0D1/F310",
          [lo for lo, _, _ in vx.IMMUTABLE] == [0xF0C3, 0xF0D1, 0xF310])
    try:
        counts = vx.self_check()
        check("PARITY: verify_cwapp_exclusions self-check passes",
              counts["entries"] == 13 and counts["forbidden_bytes"] == 5113
              and counts["immutable_bytes"] == 3 and counts["allowed_bytes"] == 7172)
    except AssertionError as exc:
        check("PARITY: verify_cwapp_exclusions self-check passes", False, str(exc))


def compiler_map_parity() -> None:
    path = ROOT / "compiler" / "src" / "targets.cpp"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    start = text.find("const Target TARGET_CWAPP{")
    if start < 0:
        check("PARITY: TARGET_CWAPP found in targets.cpp", False)
        return
    end = text.find("}};", start)
    block = text[start:end if end >= 0 else len(text)]

    pairs = [(int(a, 16), int(b, 16)) for a, b in
             re.findall(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}", block)]
    expected = [(lo, hi) for lo, hi, _ in vx.EXCLUSIONS]
    check("PARITY: 13 reserved_ram entries in targets.cpp", len(pairs) == 13, str(len(pairs)))
    check("PARITY: targets.cpp map == exclusion verifier map", pairs == expected,
          "compiler=%s" % (pairs,))

    # The Target initializer is positional: 0xE820 / 0xEE00 are the var_base
    # and var_limit fields of TARGET_CWAPP.
    check("PARITY: cwapp var_base 0xE820", "0XE820" in block.upper())
    check("PARITY: cwapp var_limit 0xEE00", "0XEE00" in block.upper())


def main() -> int:
    print("cwapp RAM map sync invariants (root=%s)" % ROOT)
    canonical_invariants()
    compiler_map_parity()
    print("\n%d checks, %d failure(s)" % (CHECKS, len(FAILS)))
    if FAILS:
        print("FAILED: " + " ".join(FAILS))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
