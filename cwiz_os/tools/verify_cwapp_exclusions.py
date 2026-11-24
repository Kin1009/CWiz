#!/usr/bin/env python3
"""Verify that a linked cwapp artifact puts no data/stack/heap symbol in the
fx-580VN X firmware's forbidden RAM ranges or immutable addresses.

Usage
-----
    python3 cwiz_os/tools/verify_cwapp_exclusions.py <path> [--json] [--report PATH]

``<path>`` may be:

* an assembly listing (``.asm``) from ``wizc --cwapp -S ...``.  The numeric
  ``L/ST/LD/STD Rn|ERn, <decimal>`` operands are the absolute addresses the
  CWiz RAM allocator assigned to globals, locals, temporaries and heap objects.
  A built ROM (``.bin``) has no symbol table of its own, so the verifier reads
  the companion ``.asm`` written beside it instead.
* a symbol/address report (``.json``/``.txt``/``.map``/``.sym``).  Each line may
  be ``NAME ADDR [SIZE] [data|stack|heap]`` with ADDR in decimal or ``0x`` hex;
  JSON accepts ``{"symbols": [{"name", "addr", "size", "kind"}, ...]}`` or
  ``{"addresses": [{"addr", "size"}, ...]}``.  ``--report PATH`` forces a
  report even when ``<path>`` is a listing.

The 13-entry exclusion table below mirrors
``compiler/src/targets.cpp: TARGET_CWAPP.reserved_ram`` (inclusive ranges).
Exit status is 0 on success and 1 on any violation or bad input.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import NamedTuple

# ---------------------------------------------------------------------------
# Authoritative cwapp RAM map -- keep in sync with compiler/src/targets.cpp.
# All ranges are INCLUSIVE and live in the flat 16-bit address space.
# ---------------------------------------------------------------------------

RAM_LOW = 0xD000                 # first byte of the app-visible RAM window
VAR_BASE = 0xE820                # globals start here (TARGET_CWAPP.var_base)
VAR_LIMIT = 0xEE00               # allocator hard limit (TARGET_CWAPP.var_limit)
HEAP_HI = 0xEDE0                 # dynamic heap watermark ceiling
STACK_LO = 0xD16C                # app stack grows down to here
STACK_HI = 0xDBCF                # app stack base (highest byte)

# Eight hard forbidden ranges: firmware/OS contract + shadow framebuffers.
FORBIDDEN_RANGES = [
    (0xD000, 0xD16B, "OS key/variable scratch below the app stack"),
    (0xDBD0, 0xDBDF, "reserved system span above the app stack"),
    (0xDBE0, 0xDBE7, "serial-number span"),
    (0xDDD3, 0xE5D2, "SB2 shadow screen buffer"),
    (0xEDE0, 0xEFFF, "reserved span above globals / heap"),
    (0xF000, 0xF04F, "SFR bank 0"),
    (0xF220, 0xF224, "SFR window"),
    (0xF800, 0xFFFF, "SB1 shadow screen buffer"),
]

# Three immutable single addresses the firmware owns outright.
IMMUTABLE = [
    (0xF0C3, 0xF0C3, "immutable firmware address"),
    (0xF0D1, 0xF0D1, "immutable firmware address"),
    (0xF310, 0xF310, "immutable firmware address"),
]

# Two conservatively excluded (guide-unmapped) spans: peek/poke there faults.
GUIDE_UNMAPPED = [
    (0xF100, 0xF21F, "guide-unmapped (peek/poke faults)"),
    (0xF311, 0xF7FF, "guide-unmapped (peek/poke faults)"),
]

# The full 13-entry reserved_ram table, in compiler order.
EXCLUSIONS = FORBIDDEN_RANGES + IMMUTABLE + GUIDE_UNMAPPED

FORBIDDEN_BYTES_EXPECTED = 5113
IMMUTABLE_BYTES_EXPECTED = 3
ALLOWED_BYTES_EXPECTED = 7172
RAM_WINDOW_BYTES = 0x10000 - RAM_LOW  # 0x3000 == 12288


class Ref(NamedTuple):
    """One concrete RAM reference: [addr, addr+size) plus provenance."""
    addr: int
    size: int
    where: str
    text: str


def self_check() -> dict:
    """Assert the byte-budget and table invariants; return the counts."""
    forbidden_bytes = sum(hi - lo + 1 for lo, hi, _ in FORBIDDEN_RANGES)
    immutable_bytes = sum(hi - lo + 1 for lo, hi, _ in IMMUTABLE)
    allowed_bytes = RAM_WINDOW_BYTES - forbidden_bytes - immutable_bytes

    assert len(EXCLUSIONS) == 13, "exclusion table must have 13 entries"
    assert forbidden_bytes == FORBIDDEN_BYTES_EXPECTED, (
        f"forbidden byte count {forbidden_bytes} != {FORBIDDEN_BYTES_EXPECTED}"
    )
    assert immutable_bytes == IMMUTABLE_BYTES_EXPECTED, (
        f"immutable byte count {immutable_bytes} != {IMMUTABLE_BYTES_EXPECTED}"
    )
    assert allowed_bytes == ALLOWED_BYTES_EXPECTED, (
        f"allowed byte count {allowed_bytes} != {ALLOWED_BYTES_EXPECTED}"
    )
    assert forbidden_bytes + immutable_bytes + allowed_bytes == RAM_WINDOW_BYTES == 12288

    # Every entry must be a valid, non-overlapping, in-RAM inclusive range.
    seen = {}
    for lo, hi, reason in EXCLUSIONS:
        assert RAM_LOW <= lo <= hi <= 0xFFFF, f"bad exclusion 0x{lo:04X}..0x{hi:04X}"
        assert reason, "exclusion must carry a rationale"
        for addr in range(lo, hi + 1):
            assert addr not in seen, (
                f"0x{addr:04X} in both {seen.get(addr)} and {reason}"
            )
            seen[addr] = reason

    return {
        "entries": len(EXCLUSIONS),
        "forbidden_bytes": forbidden_bytes,
        "immutable_bytes": immutable_bytes,
        "allowed_bytes": allowed_bytes,
        "ram_window_bytes": RAM_WINDOW_BYTES,
    }


# ---------------------------------------------------------------------------
# Input parsing
# ---------------------------------------------------------------------------

# Allocator-generated absolute memory accesses are emitted in decimal, e.g.
# `ST ER0, 59428` for a global at 0xE824.  Hex-H operands (`st r0, 0F041H`)
# belong to the hand-written runtime template -- SFRs, shadow buffers and the
# exit() save area -- and are deliberately NOT treated as allocator symbols.
ALLOC_ACCESS = re.compile(
    r"\b(L|ST|LD|STD)\s+(ER\d+|R\d+),\s*(\d+)\b", re.IGNORECASE
)
def _parse_number(token: str) -> int | None:
    token = token.strip()
    try:
        if token.lower().startswith("0x"):
            return int(token, 16)
        if token[-1:] in "Hh":
            return int(token[:-1], 16)
        return int(token, 10)
    except ValueError:
        return None


def parse_listing(text: str) -> list[Ref]:
    """Extract allocator RAM references from an assembly listing."""
    refs: list[Ref] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        code = line.split(";", 1)[0]
        for match in ALLOC_ACCESS.finditer(code):
            addr = int(match.group(3))
            if RAM_LOW <= addr <= 0xFFFF:
                width = 2 if match.group(2).upper().startswith("ER") else 1
                refs.append(Ref(addr, width, f"line {lineno}", line.strip()))
    return refs


def parse_report_text(text: str) -> list[Ref]:
    """Parse `NAME ADDR [SIZE] [kind]` (or JSON) symbol/address reports."""
    stripped = text.lstrip()
    if stripped.startswith("{"):
        return _parse_report_json(json.loads(stripped))

    refs: list[Ref] = []
    for lineno, line in enumerate(text.splitlines(), 1):
        code = line.split("#", 1)[0].split(";", 1)[0].strip()
        if not code:
            continue
        tokens = code.replace(",", " ").split()
        addr = size = None
        idx = None
        for i, tok in enumerate(tokens):
            n = _parse_number(tok)
            if n is not None and 0 <= n <= 0xFFFF:
                if addr is None:
                    addr, idx = n, i
                elif size is None:
                    size = n
                break
        if addr is None:
            continue
        name = tokens[0] if idx != 0 else "symbol"
        if size is None or size < 1:
            size = 1
        refs.append(Ref(addr, size, f"report line {lineno} ({name})", code))
    return refs


def _parse_report_json(data: dict) -> list[Ref]:
    refs: list[Ref] = []
    entries = []
    if isinstance(data.get("symbols"), list):
        entries = data["symbols"]
    elif isinstance(data.get("addresses"), list):
        entries = data["addresses"]
    elif isinstance(data, list):
        entries = data
    for i, ent in enumerate(entries):
        if not isinstance(ent, dict):
            continue
        raw = ent.get("addr", ent.get("address"))
        addr = _parse_number(str(raw)) if raw is not None else None
        if addr is None or not (0 <= addr <= 0xFFFF):
            continue
        size = ent.get("size", ent.get("width", 1))
        size = _parse_number(str(size)) or 1
        name = ent.get("name", f"symbol[{i}]")
        refs.append(Ref(addr, size, f"report {name}", json.dumps(ent, sort_keys=True)))
    return refs


def resolve_refs(path: Path, report: Path | None) -> tuple[list[Ref], str]:
    """Read the requested input and return (refs, human-readable source)."""
    if report is not None:
        text = report.read_text(encoding="utf-8", errors="replace")
        refs = parse_report_text(text)
        return refs, f"symbol report {report}"

    if not path.exists():
        raise SystemExit(f"input not found: {path}")

    suffix = path.suffix.lower()
    if suffix in {".bin", ".rom", ".img"}:
        companion = path.with_suffix(".asm")
        if not companion.exists():
            raise SystemExit(
                f"{path} is a ROM image with no symbol table; expected the "
                f"companion assembly listing {companion} (wizc writes it beside "
                f"the .bin unless --no-asm was used), or pass --report PATH"
            )
        path = companion
        suffix = ".asm"

    text = path.read_text(encoding="utf-8", errors="replace")
    if suffix in {".json", ".map", ".sym", ".txt", ".report"} or text.lstrip().startswith("{"):
        return parse_report_text(text), f"symbol report {path}"
    return parse_listing(text), f"assembly listing {path}"


# ---------------------------------------------------------------------------
# Verification
# ---------------------------------------------------------------------------

def find_exclusion(addr: int) -> tuple[int, int, str] | None:
    for lo, hi, reason in EXCLUSIONS:
        if lo <= addr <= hi:
            return lo, hi, reason
    return None


def verify(refs: list[Ref]) -> tuple[list[str], dict]:
    errors: list[str] = []
    hits = {"forbidden": 0, "immutable": 0, "guide_unmapped": 0, "out_of_window": 0}

    for ref in refs:
        end = ref.addr + ref.size - 1
        # Check every byte of a multi-byte access.
        category = None
        hit = None
        for addr in range(ref.addr, end + 1):
            found = find_exclusion(addr)
            if found is not None:
                category, hit = found, addr
                break
        if hit is not None:
            lo, hi, reason = category
            if (lo, hi) in {(a, b) for a, b, _ in IMMUTABLE}:
                kind = "immutable"
            elif (lo, hi) in {(a, b) for a, b, _ in FORBIDDEN_RANGES}:
                kind = "forbidden"
            else:
                kind = "guide_unmapped"
            hits[kind] += 1
            errors.append(
                f"{ref.where}: symbol/reference 0x{ref.addr:04X}..0x{end:04X} "
                f"lands in {kind} 0x{lo:04X}..0x{hi:04X} ({reason})\n"
                f"    {ref.text}"
            )
            continue

        # Not excluded: it must still sit in a sanctioned allocation window.
        allowed = (
            (STACK_LO <= ref.addr and end <= STACK_HI)
            or (VAR_BASE <= ref.addr and end < VAR_LIMIT)
        )
        if not allowed:
            hits["out_of_window"] += 1
            errors.append(
                f"{ref.where}: reference 0x{ref.addr:04X}..0x{end:04X} is outside "
                f"the stack window 0x{STACK_LO:04X}..0x{STACK_HI:04X} and the "
                f"allocator window 0x{VAR_BASE:04X}..0x{VAR_LIMIT - 1:04X}"
            )

    return errors, hits


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", type=Path, help="linked .asm/.bin or symbol/address report")
    ap.add_argument("--report", type=Path, help="force reading this symbol/address report")
    ap.add_argument("--json", action="store_true", help="emit a JSON summary")
    ap.add_argument("--self-check-only", action="store_true",
                    help="only print the table invariants and exit")
    args = ap.parse_args()

    counts = self_check()
    if args.self_check_only:
        print(json.dumps(counts, indent=2) if args.json else
              "cwapp exclusion table OK: "
              f"{counts['forbidden_bytes']} forbidden + "
              f"{counts['immutable_bytes']} immutable + "
              f"{counts['allowed_bytes']} allowed = "
              f"{counts['ram_window_bytes']} bytes")
        return

    refs, source = resolve_refs(args.path, args.report)
    if not refs:
        raise SystemExit(
            f"no cwapp RAM references found in {source}; nothing to verify "
            "(use --report with a symbol/address report if needed)"
        )

    errors, hits = verify(refs)
    summary = {
        "ok": not errors,
        "source": source,
        "references": len(refs),
        "hits": hits,
        "table": counts,
        "errors": errors,
    }

    if args.json:
        print(json.dumps(summary, indent=2))
    elif errors:
        print(f"cwapp RAM exclusion verification FAILED ({len(errors)} issue(s)):")
        for err in errors[:40]:
            print("  FAIL:", err)
    else:
        print(
            f"cwapp RAM exclusion verification OK: {len(refs)} references from "
            f"{source}; 0 forbidden, 0 immutable, 0 guide-unmapped hits "
            f"(5113 forbidden + 3 immutable + 7172 allowed = 12288 bytes)"
        )
    raise SystemExit(0 if not errors else 1)


if __name__ == "__main__":
    main()
