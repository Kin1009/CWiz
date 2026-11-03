#!/usr/bin/env python3
"""Generate the build's CWiz serial macro from source, date, model and entropy."""

import argparse
from datetime import datetime
import hashlib
import secrets
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--project", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--model", default="0580")
    parser.add_argument("--rom", type=Path)
    parser.add_argument("--hash-only", action="store_true")
    args = parser.parse_args()

    model = args.model.upper()
    if len(model) != 4 or any(char not in "0123456789ABCDEF" for char in model):
        raise SystemExit("model code must be exactly four hexadecimal characters")

    # Fixed source order: project-relative .cwiz paths sorted lexicographically.
    # The generated build/serial_number.cwiz is excluded to avoid self-reference.
    sources = sorted(
        path for path in args.project.rglob("*.cwiz")
        if "build" not in path.relative_to(args.project).parts
    )
    if not sources:
        raise SystemExit("no authored .cwiz source files found")

    digest = hashlib.sha256()
    for source in sources:
        digest.update(source.read_bytes())
    suffix = digest.hexdigest()[-6:].upper()

    today = datetime.now()
    day_month = today.strftime("%d%m")
    year = today.strftime("%y")
    random_hex = secrets.token_hex(4).upper()
    serial = (
        f"{model}-{day_month}-{year}{suffix[:2]}-{suffix[2:]}-"
        f"{random_hex[:4]}-{random_hex[4:]}"
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    serial_bytes = bytes.fromhex(serial.replace("-", ""))
    byte_list = ", ".join(f"0x{value:02X}" for value in serial_bytes)
    args.output.write_text(
        f'#define CWIZ_SERIAL_NUMBER "{serial}"\n'
        f'#define CWIZ_SERIAL_BYTE_LIST {byte_list}\n',
        encoding="ascii",
    )
    if not args.hash_only:
        print(f"CWiz serial: {serial}")
        print("Serial hash source order:")
        for source in sources:
            print(f"  {source.relative_to(args.project).as_posix()}")


if __name__ == "__main__":
    main()
