#!/usr/bin/env python3
"""Compile the firmware-decompiler CWiz corpus with wizc.

No /tmp: reports and logs live under <repo>/temp/corpus by default.  The corpus
itself is read-only; failures are reported to the ROM agent, never fixed here.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--wizc", default=str(Path(__file__).resolve().parents[1] / "build" / "wizc"))
    ap.add_argument("--corpus", default=str(Path(__file__).resolve().parents[1] /
                                          "firmware-re" / "fx580" / "cwiz" / "auto"))
    ap.add_argument("--named", action="store_true", help="also check cwiz/named")
    ap.add_argument("--out", default=str(Path(__file__).resolve().parents[1] /
                                         "temp" / "corpus"))
    ap.add_argument("--max-errors", type=int, default=30)
    args = ap.parse_args()

    corpus = Path(args.corpus)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    wizc = Path(args.wizc).resolve()
    if not wizc.exists():
        print(f"error: wizc not found: {wizc}", file=sys.stderr)
        return 2

    manifest = corpus / "MANIFEST.json"
    files = []
    if manifest.exists():
        mods = json.loads(manifest.read_text(encoding="utf-8")).get("modules", [])
        files = [corpus / m for m in mods]
    if not files:
        files = sorted(p for p in corpus.glob("*.cwiz") if not p.name.startswith("_"))
    if args.named:
        named = corpus.parent / "named"
        if named.exists():
            files += sorted(named.glob("*.cwiz"))

    failures = []
    started = time.time()
    for i, path in enumerate(files, 1):
        # -S --no-asm compiles and assembles without writing .bin/.asm files.
        proc = subprocess.run([str(wizc), "-S", "--no-asm", str(path)],
                              cwd=str(corpus), text=True, capture_output=True)
        if proc.returncode != 0:
            failures.append((str(path), (proc.stderr or proc.stdout).strip()))
            if len(failures) <= args.max_errors:
                print(f"FAIL {path}")
                for line in (proc.stderr or proc.stdout).strip().splitlines()[:8]:
                    print("   " + line)
        if i % 200 == 0:
            print(f"  {i}/{len(files)} ({time.time() - started:.1f}s)")

    elapsed = time.time() - started
    report = out / "report.txt"
    with report.open("w", encoding="utf-8") as f:
        f.write(f"corpus: {corpus}\n")
        f.write(f"modules: {len(files)}\n")
        f.write(f"passed: {len(files) - len(failures)}\n")
        f.write(f"failed: {len(failures)}\n")
        f.write(f"seconds: {elapsed:.2f}\n")
        for path, err in failures:
            f.write(f"\n=== {path}\n{err}\n")
    print(f"{len(files) - len(failures)}/{len(files)} passed in {elapsed:.2f}s -> {report}")
    return 0 if not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
