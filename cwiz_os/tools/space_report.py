#!/usr/bin/env python3
"""Report boot-payload and bank-3 blob space use for the integrated CWiz OS.

This is a *measurement* tool.  It never edits a ROM, a cwiz tree, a ledger or
build artifacts.  It reads the compiled images in ``cwiz_os/build`` and the
generated blob sources, and prints:

  * exact payload / recovered / idiomatic / combined byte counts and the
    remaining bank-3 gap headroom (with a delta against an optional baseline
    ``final-manifest.json``);
  * the largest reducible *data* costs in the boot payload (rom8 tables and
    string literals) with byte counts;
  * per-batch module counts in the generated idiomatic blob;
  * cross-blob duplicate function definitions -- structural linked/deep copies
    of functions that also have a curated idiomatic port.  Those are the copies
    the Makefile drops via ``RECOVERED_DUP_EXCLUDE``.

Usage
-----
    python3 tools/space_report.py
    python3 tools/space_report.py --baseline build/final-manifest.json
    python3 tools/space_report.py --json

Paths are resolved relative to this script's parent (the cwiz_os project), so
the report is reproducible from any working directory.  All text I/O is UTF-8
without BOM.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
BUILD = PROJECT / "build"
FW = PROJECT.parent / "firmware-re" / "fx580"

PAYLOAD_BUDGET = 0x7B0E            # 31,502 B, first protected stock key map
GAP_START = 0x30E40
GAP_END = 0x3FFC0
GAP_BYTES = GAP_END - GAP_START    # 61,824 B

# A definition at the start of a line.  Calls such as `if (comparisons...)` in
# comments are deliberately NOT counted.
FUNC_RE = re.compile(
    r"^\s*(?:static\s+)?(?:void|int|char|long|short|unsigned|rom8|signed|const)\s+"
    r"(\w+)\s*\(", re.M)
INCLUDE_RE = re.compile(r'#include\s+"([^"]+)"')
ARRAY_RE = re.compile(
    r"\brom8\s+char\s+(\w+)\s*\[\s*(\d+)\s*\]")
STRING_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def size(path: Path) -> int | None:
    return path.stat().st_size if path.is_file() else None


def sha256(path: Path) -> str | None:
    import hashlib
    return hashlib.sha256(path.read_bytes()).hexdigest().upper() if path.is_file() else None


def transitive_functions(path: Path, seen: set[str] | None = None) -> set[str]:
    """Function names defined by ``path`` and its nested #includes."""
    if seen is None:
        seen = set()
    p = Path(path)
    if not p.is_file() or str(p) in seen:
        return set()
    seen.add(str(p))
    try:
        text = p.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return set()
    out = set(FUNC_RE.findall(text))
    for inc in INCLUDE_RE.findall(text):
        out |= transitive_functions((p.parent / inc).resolve(), seen)
    return out


def include_paths(source: Path) -> list[Path]:
    return [Path(m.group(1)) for m in INCLUDE_RE.finditer(
        source.read_text(encoding="utf-8"))]


def source_data_costs() -> list[dict]:
    """Largest rom8 tables and string literals in the boot-payload sources."""
    roots = [
        PROJECT / "main.cwiz",
        PROJECT / "system.cwiz",
        PROJECT / "launcher/launcher.cwiz",
        PROJECT / "launcher/menu_icons.cwiz",
        PROJECT / "diagnostics/diagnostic_menu.cwiz",
    ]
    roots += sorted((PROJECT / "apps").rglob("*.cwiz"))
    rows: list[dict] = []
    for path in roots:
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for name, n in ARRAY_RE.findall(text):
            rows.append({"kind": "rom8", "name": name, "bytes": int(n),
                         "source": str(path.relative_to(PROJECT))})
        # Count each distinct literal once per source file (each occurrence
        # still costs bytes, so sum all occurrences).
        for m in STRING_RE.finditer(text):
            s = m.group(1)
            if len(s) < 4:            # skip punctuation/format one-liners
                continue
            rows.append({"kind": "string", "name": s[:48], "bytes": len(s) + 1,
                         "source": str(path.relative_to(PROJECT))})
    rows.sort(key=lambda r: (-r["bytes"], r["source"]))
    return rows


def blob_module_counts(source: Path) -> dict:
    counts: dict[str, int] = {}
    for inc in include_paths(source):
        rel = str(inc)
        batch = next((b for b in ("bank0", "bank1a", "bank1b", "bank2",
                                  "tails", "stackframes", "flags", "deep",
                                  "linked", "shim") if f"/{b}/" in rel or
                      rel.endswith(f"/{b}")), "other")
        counts[batch] = counts.get(batch, 0) + 1
    return counts


def duplicate_functions(recovered_src: Path, idiomatic_src: Path) -> list[dict]:
    """Idiomatic includes whose functions are already defined in the recovered
    blob's linked/deep sources (or vice versa).  The Makefile keeps the higher-
    evidence idiomatic copy and drops the structural one."""
    rec = include_paths(recovered_src)
    idi = include_paths(idiomatic_src)
    rec_funcs = {str(f): transitive_functions(f) for f in rec}
    rec_all: set[str] = set()
    for fs in rec_funcs.values():
        rec_all |= fs
    idi_dupes = []
    for f in idi:
        fs = transitive_functions(f)
        if fs and fs <= rec_all:
            idi_dupes.append({"module": f.name, "functions": sorted(fs)})
    # Structural top-level files that define only functions also covered by an
    # idiomatic module: these are the RECOVERED_DUP_EXCLUDE set.
    idi_all: set[str] = set()
    for f in idi:
        idi_all |= transitive_functions(f)
    rec_dupes = [f.name for f in rec if rec_funcs[str(f)]
                 and rec_funcs[str(f)] <= idi_all]
    return {"idiomatic_copies": idi_dupes, "structural_copies": sorted(rec_dupes)}


def report(baseline: str | None, as_json: bool) -> int:
    payload = BUILD / "cwiz-os-program.bin"
    rec = BUILD / "recovered-blob.bin"
    idi = BUILD / "idiomatic-blob.bin"
    combined = BUILD / "recovered-combined-blob.bin"
    rom = BUILD / "cwiz-os-integrated.rom"

    p, rb, ib, cb = (size(x) for x in (payload, rec, idi, combined))
    data = {
        "payload": {"bytes": p, "budget": PAYLOAD_BUDGET,
                    "headroom": (PAYLOAD_BUDGET - p) if p is not None else None},
        "blobs": {
            "recovered": {"bytes": rb, "base": hex(GAP_START)},
            "idiomatic": {"bytes": ib,
                          "base": hex(GAP_START + rb) if rb is not None else None},
            "combined": {"bytes": cb, "gap_bytes": GAP_BYTES,
                         "headroom": (GAP_BYTES - cb) if cb is not None else None},
        },
        "rom": {"path": str(rom), "bytes": size(rom), "sha256": sha256(rom)},
    }
    if baseline:
        base = json.loads(Path(baseline).read_text(encoding="utf-8"))
        bp = base.get("payload", {})
        bb = base.get("blobs", {})
        data["delta_vs_baseline"] = {
            "payload_bytes": (p - bp["bytes"]) if p is not None and "bytes" in bp else None,
            "recovered_bytes": (rb - bb.get("recovered", {}).get("bytes", 0))
            if rb is not None else None,
            "idiomatic_bytes": (ib - bb.get("idiomatic", {}).get("bytes", 0))
            if ib is not None else None,
            "combined_bytes": (cb - bb.get("combined", {}).get("bytes", 0))
            if cb is not None else None,
            "gap_headroom": ((GAP_BYTES - cb) -
                             bb.get("combined", {}).get("unused_gap", 0))
            if cb is not None else None,
        }

    data["reducible_payload_data"] = source_data_costs()[:12]
    data["blob_modules"] = blob_module_counts(BUILD / "idiomatic-blob-source.cwiz")
    data["duplicates"] = duplicate_functions(
        BUILD / "recovered-blob-source.cwiz",
        BUILD / "idiomatic-blob-source.cwiz")

    if as_json:
        print(json.dumps(data, indent=2, ensure_ascii=False))
        return 0

    print("CWiz OS space report")
    print(f"  payload      {p} / {PAYLOAD_BUDGET} B  "
          f"({PAYLOAD_BUDGET - p} B headroom)")
    print(f"  recovered    {rb} B at {hex(GAP_START)}")
    print(f"  idiomatic    {ib} B at {hex(GAP_START + rb)}")
    print(f"  combined     {cb} B / {GAP_BYTES} B gap  "
          f"({GAP_BYTES - cb} B headroom)")
    print(f"  ROM          {size(rom)} B  sha256={data['rom']['sha256']}")
    if "delta_vs_baseline" in data:
        d = data["delta_vs_baseline"]
        print("  delta vs baseline: recovered %+d B, idiomatic %+d B, "
              "combined %+d B, gap headroom %+d B" % (
                  d["recovered_bytes"], d["idiomatic_bytes"],
                  d["combined_bytes"], d["gap_headroom"]))
    print("\nLargest reducible payload data:")
    for r in data["reducible_payload_data"][:8]:
        print(f"  {r['bytes']:6d} B  {r['kind']:6s} {r['name']:44s} "
              f"[{r['source']}]")
    print("\nIdiomatic blob modules by batch:", data["blob_modules"])
    dup = data["duplicates"]
    print("Cross-blob duplicate functions: %d structural copies, %d idiomatic "
          "copies" % (len(dup["structural_copies"]), len(dup["idiomatic_copies"])))
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--baseline", help="baseline final-manifest.json for deltas")
    ap.add_argument("--json", action="store_true", help="emit JSON")
    args = ap.parse_args()
    return report(args.baseline, args.json)


if __name__ == "__main__":
    raise SystemExit(main())
