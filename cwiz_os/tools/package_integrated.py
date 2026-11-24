#!/usr/bin/env python3
"""Package the integrated CWiz OS ROM: boot payload + recovered-code blob.

Like ``package_standalone.py`` this places the compiled CWiz OS program at
linear offset 0 (the fx-580 boot area).  Unlike it, this packager ALSO places a
second compiled blob -- the recovered firmware routines that no longer fit in
the 31,502-byte (0x7B0E) boot overlay -- in a preserved region above the
payload.  The default region is the blank bank-3 flash gap starting at 3:0E40.

Hard rules enforced here:
  * the boot payload stays within the 0x7B0E budget;
  * neither blob may overwrite a protected region from
    ``package_standalone.STOCK_REGIONS`` (key maps, reset path, reset stub);
  * the second blob must fit entirely inside the measured bank-3 blank gap;
  * the fx-580 checksum at 0x3FFF6 is recomputed over the FINAL image;
  * the stock ROM source and all shared analysis data are read-only inputs.

By default the recovered blob (compiled at origin 0 with ``wizc --target cw``)
is preserved verbatim as a code artifact: its absolute B/BL targets and ROM data
addresses still assume bank 0.  With ``--relocate-blob`` the blob is passed
through ``tools/relocate_blob.py`` first, so the image placed at ``--blob-base``
is self-consistent at that bank and can be executed in place.  The relocation
map is written next to the ROM and every safety check below runs on the final
bytes either way.  ``--verify`` rebuilds the same relocation before comparing.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

# The standalone packager is the authority for protected regions, the budget,
# the clobber report, the checksum algorithm and the model-image writer.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from package_standalone import (  # noqa: E402
    CHECKSUM_OFFSET,
    PAYLOAD_BYTES,
    ROM_SIZE,
    STOCK_REGIONS,
    check_budget,
    clobbered_regions,
    fx580_checksum,
    print_clobber_report,
    write_model_image,
)

# Measured from the stock ROM: one contiguous erased (0xFF) run in bank 3.
# Linear 0x30E40 == bank 3 offset 0x0E40; 0x3FFC0 == bank 3 offset 0xFFC0,
# where the stock ROM's tail metadata begins.  Do not change without re-reading
# the stock ROM and re-checking every protected region.
BANK3_GAP_START = 0x30E40
BANK3_GAP_END = 0x3FFC0
BANK3_GAP_BYTES = BANK3_GAP_END - BANK3_GAP_START  # 0xF180 = 61,824


def overlaps(a0: int, a1: int, b0: int, b1: int) -> bool:
    return a0 < b1 and b0 < a1


def banked(addr: int) -> str:
    return f"{addr >> 16}:{addr & 0xFFFF:04X}"


def check_blob(blob: bytes, base: int) -> int:
    """Validate the recovered blob placement.  Returns the exclusive end."""
    end = base + len(blob)
    if not blob:
        raise SystemExit("recovered blob is empty")

    # 1. Protected stock regions (the same authority as the standalone packager).
    for start, region_end, description, protected in STOCK_REGIONS:
        if protected and overlaps(base, end, start, region_end):
            raise SystemExit(
                "\n".join([
                    "recovered blob would overwrite a PROTECTED stock region",
                    f"  blob: {banked(base)}..{banked(end - 1)} ({len(blob)} B)",
                    f"  region: {banked(start)}..{banked(region_end - 1)} "
                    f"[{description}]",
                ])
            )

    # 2. The measured bank-3 blank gap.
    if base < BANK3_GAP_START or end > BANK3_GAP_END:
        raise SystemExit(
            "\n".join([
                "recovered blob does not fit in the preserved bank-3 gap",
                f"  blob: {banked(base)}..{banked(end - 1)} ({len(blob)} B)",
                f"  gap:  {banked(BANK3_GAP_START)}..{banked(BANK3_GAP_END - 1)} "
                f"({BANK3_GAP_BYTES} B)",
                f"  overflow: {end - BANK3_GAP_END} B" if end > BANK3_GAP_END else
                f"  underflow: {BANK3_GAP_START - base} B",
            ])
        )

    # 3. The ROM checksum word must never be touched by the blob.  (The boot
    #    payload is written at 0 by design, independently of the blob.)
    if overlaps(base, end, CHECKSUM_OFFSET, CHECKSUM_OFFSET + 2):
        raise SystemExit(
            f"recovered blob would overwrite the ROM checksum at "
            f"{banked(CHECKSUM_OFFSET)}"
        )
    return end


def print_placement_map(program_bytes: int, blob_bytes: int, blob_base: int,
                        blob_end: int, checksum: int, relocated: bool = False) -> None:
    print("Placement map (linear ROM offsets):")
    print(f"  [0x000000..0x{program_bytes - 1:06X}]  ({program_bytes:6d} B)  "
          "CWiz OS boot payload  [0:0000..., by design]")
    if blob_bytes:
        kind = "relocated" if relocated else "preserved"
        print(f"  [0x{blob_base:06X}..0x{blob_end - 1:06X}]  ({blob_bytes:6d} B)  "
              f"recovered firmware blob  [{banked(blob_base)}..., {kind} gap]")
        print(f"  [0x{blob_end:06X}..0x{BANK3_GAP_END - 1:06X}]  "
              f"({BANK3_GAP_END - blob_end:6d} B)  unused bank-3 gap  "
              f"[{banked(blob_end)}..{banked(BANK3_GAP_END - 1)}]")
    print(f"  [0x{CHECKSUM_OFFSET:06X}..0x{CHECKSUM_OFFSET + 1:06X}]  (     2 B)  "
          f"fx-580 ROM checksum = 0x{checksum:04X}")


def build_image(program: bytes, blob: bytes, blob_base: int, base: bytes) -> bytearray:
    image = bytearray(base)
    image[: len(program)] = program
    if blob:
        image[blob_base : blob_base + len(blob)] = blob
    checksum = fx580_checksum(image)
    image[CHECKSUM_OFFSET : CHECKSUM_OFFSET + 2] = checksum.to_bytes(2, "little")
    return image


def prepare_blob(args: argparse.Namespace, blob: bytes) -> tuple[bytes, dict | None]:
    """Relocate the compiled-at-0 blob in memory when --relocate-blob is set.

    Without the flag the original 'preserved artifact' behaviour is unchanged:
    the blob is copied verbatim.  With the flag the bank-aware relocator from
    ``relocate_blob.py`` rewrites absolute B/BL targets and ROM data offsets for
    ``--blob-base`` and emits a JSON relocation map.
    """
    if not args.relocate_blob or not blob:
        return blob, None
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from relocate_blob import relocate  # noqa: E402

    relocated, rmap = relocate(
        blob, args.blob_base, data_start=args.blob_data_start
    )
    if len(relocated) != len(blob):
        raise SystemExit("relocation changed the blob size")
    map_path = args.relocation_map or args.rom_out.with_suffix(
        args.rom_out.suffix + ".relocations.json"
    )
    map_path.parent.mkdir(parents=True, exist_ok=True)
    map_path.write_text(json.dumps(rmap, indent=2) + "\n", encoding="utf-8")
    c = rmap["counts"]
    print(
        f"Relocated blob: {c['branches']} branches "
        f"({c['B']} B, {c['BL']} BL), {c['data_refs']} ROM data addresses "
        f"-> {banked(args.blob_base)}; map: {map_path}"
    )
    return relocated, rmap


def package(args: argparse.Namespace) -> None:
    program = args.program.read_bytes()
    blob = args.blob.read_bytes() if args.blob and args.blob.is_file() else b""
    blob, rmap = prepare_blob(args, blob)
    base = args.base_rom.read_bytes()
    if len(base) != ROM_SIZE:
        raise SystemExit(f"base ROM must be {ROM_SIZE} bytes, got {len(base)}")
    if not program or len(program) >= CHECKSUM_OFFSET:
        raise SystemExit("CWiz boot payload is empty or overlaps the ROM metadata/checksum")

    check_budget(len(program))
    print(
        f"Payload budget: {len(program)} / {PAYLOAD_BYTES} bytes "
        f"(0x{len(program):X} / 0x{PAYLOAD_BYTES:X}), "
        f"{PAYLOAD_BYTES - len(program)} bytes free"
    )
    print_clobber_report(len(program))

    blob_base = args.blob_base
    blob_end = check_blob(blob, blob_base) if blob else blob_base
    if blob:
        print(
            f"Recovered blob: {len(blob)} bytes at 0x{blob_base:X} "
            f"({banked(blob_base)}), ends 0x{blob_end:X}; "
            f"{BANK3_GAP_END - blob_end} bytes of bank-3 gap left"
        )
    else:
        print("Recovered blob: none supplied")

    image = build_image(program, blob, blob_base, base)
    if len(image) != ROM_SIZE:
        raise SystemExit(f"internal error: built image is {len(image)} bytes")
    checksum = int.from_bytes(image[CHECKSUM_OFFSET : CHECKSUM_OFFSET + 2], "little")
    if checksum != fx580_checksum(image):
        raise SystemExit("internal error: ROM checksum verification failed")

    print_placement_map(len(program), len(blob), blob_base, blob_end, checksum,
                        relocated=bool(args.relocate_blob))

    args.rom_out.parent.mkdir(parents=True, exist_ok=True)
    args.rom_out.write_bytes(image)

    model = args.model_out
    model.mkdir(parents=True, exist_ok=True)
    for name in ("config.bin", "interface.png"):
        shutil.copy2(args.model_template / name, model / name)
    ram_template = args.ram_template or (args.base_rom.parent / "ram.dmp")
    seeded_ram = write_model_image(model, image, ram_template)

    digest = hashlib.sha256(image).hexdigest().upper()
    print(f"Integrated fx-580 ROM: {args.rom_out} ({len(image)} bytes)")
    print(f"ROM SHA-256: {digest}")
    print(f"ROM checksum: 0x{checksum:04X}")
    print(f"Emulator model: {model}")
    print(f"  rom.bin: {len(image)} bytes")
    print(f"  wiz/diagnostic-menu.bin: {len(image)} bytes")
    if seeded_ram:
        print(f"  ram.dmp: seeded from {ram_template}")
    else:
        print("  ram.dmp: not seeded; first boot runs the CWiz recovery path")


def verify(args: argparse.Namespace) -> None:
    """Re-open a packaged ROM and check size, checksum and placed blobs."""
    image = args.rom_out.read_bytes()
    problems: list[str] = []
    if len(image) != ROM_SIZE:
        problems.append(f"size {len(image)} != {ROM_SIZE}")
    stored = int.from_bytes(image[CHECKSUM_OFFSET : CHECKSUM_OFFSET + 2], "little")
    computed = fx580_checksum(image)
    if stored != computed:
        problems.append(f"checksum {stored:04X} != recomputed {computed:04X}")

    program = args.program.read_bytes()
    if image[: len(program)] != program:
        problems.append("boot payload does not match --program")

    blob = args.blob.read_bytes() if args.blob and args.blob.is_file() else b""
    if blob:
        blob, _ = prepare_blob(args, blob)
        check_blob(blob, args.blob_base)
        end = args.blob_base + len(blob)
        if image[args.blob_base : end] != blob:
            kind = "relocated" if args.relocate_blob else "verbatim"
            problems.append(
                f"recovered blob ({kind}) does not match --blob at --blob-base"
            )
        if args.relocate_blob and args.relocation_map:
            rmap = json.loads(args.relocation_map.read_text(encoding="utf-8"))
            if rmap.get("relocated_sha256") != hashlib.sha256(blob).hexdigest().upper():
                problems.append("relocation map SHA-256 does not match rebuilt blob")
    else:
        end = args.blob_base

    if problems:
        raise SystemExit("integrated ROM verification FAILED: " + "; ".join(problems))
    print(
        f"Integrated ROM OK: {len(image)} bytes, checksum 0x{stored:04X}, "
        f"payload {len(program)} B, recovered blob {len(blob)} B at "
        f"0x{args.blob_base:X}, placement within {banked(BANK3_GAP_START)}.."
        f"{banked(BANK3_GAP_END - 1)}"
    )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--program", required=True, type=Path)
    parser.add_argument("--blob", type=Path, help="compiled recovered-code blob")
    parser.add_argument("--blob-base", type=lambda s: int(s, 0), default=BANK3_GAP_START,
                        help=f"linear load address of the blob (default 0x{BANK3_GAP_START:X})")
    parser.add_argument("--relocate-blob", action="store_true",
                        help="bank-relocate a blob compiled at origin 0 for --blob-base "
                             "(default: preserve it verbatim as a code artifact)")
    parser.add_argument("--relocation-map", type=Path,
                        help="JSON relocation map output "
                             "(default: <rom-out>.relocations.json)")
    parser.add_argument("--blob-data-start", type=lambda s: int(s, 0),
                        help="first ROM-data address in the original blob (auto-detected if omitted)")
    parser.add_argument("--base-rom", type=Path)
    parser.add_argument("--rom-out", required=True, type=Path)
    parser.add_argument("--model-template", type=Path)
    parser.add_argument("--model-out", type=Path)
    parser.add_argument("--ram-template", type=Path)
    parser.add_argument("--verify", action="store_true",
                        help="verify an existing ROM instead of building one")
    args = parser.parse_args()
    if args.verify:
        verify(args)
    else:
        if not args.base_rom:
            parser.error("--base-rom is required when packaging")
        if not (args.model_template and args.model_out):
            parser.error("--model-template and --model-out are required when packaging")
        package(args)


if __name__ == "__main__":
    main()
