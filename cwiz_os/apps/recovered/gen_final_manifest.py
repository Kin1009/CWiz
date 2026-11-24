#!/usr/bin/env python3
"""Generate cwiz_os/build/final-manifest.json from the ledgers and build artifacts.

Called by `make final-manifest` (and therefore at the end of `make integrated`).
Lives next to the recovered-app source because that is a writable path owned by
the final-integration agent; it never edits any cwiz tree, ledger or ROM source.
An existing `emulator` block is preserved so the headless-boot evidence can be
added after the build without being lost by a later rebuild.
"""
import datetime
import hashlib
import json
import pathlib
import sys


def main() -> None:
    proj = pathlib.Path(sys.argv[1])
    build = pathlib.Path(sys.argv[2])
    fw = proj.parent / "firmware-re" / "fx580"

    def rd(p):
        with open(p, encoding="utf-8") as f:
            return json.load(f)

    def size(p):
        return p.stat().st_size if p.exists() else None

    tails = rd(fw / "analysis/idiomatic_tails.json")
    flags = rd(fw / "analysis/idiomatic_flags.json")
    sf = rd(fw / "analysis/idiomatic_stackframes.json")
    sweep = rd(fw / "analysis/idiomatic_sweep_report.json")

    src = (build / "idiomatic-blob-source.cwiz").read_text(encoding="utf-8")
    incs = [ln.split('"')[1] for ln in src.splitlines() if ln.startswith('#include "')]

    def norm(p):
        try:
            return str(pathlib.Path(p).relative_to(fw))
        except ValueError:
            return p

    incs = [norm(p) for p in incs]
    included_paths = set(incs)
    tmap = {x["module"]: x for x in tails["converted"]}
    fmap = {x["module"]: x for x in flags["converted"]}
    smap = {
        v["module"]: (k, v)
        for k, v in sf["functions"].items()
        if v.get("status") == "converted"
    }

    included = []
    for p in incs:
        if "/tails/" in p and p in tmap:
            dy = tmap[p]["equivalence"]["dynamic"]
            ev = "real wizrun differential %d/%d vectors equivalent; static proof pass" % (
                dy["vectors_equivalent"], dy["vectors_executed"])
            tier, fid = "A-tails-real-dynamic", tmap[p]["id"]
        elif "/stackframes/" in p and p in smap:
            fid, v = smap[p]
            dy = v.get("dynamic")
            ev = ("real wizrun differential %d/%d pairs equivalent; frame certificate pass"
                  % (dy["equivalent"], dy["pairs"])) if dy else (
                  "frame certificate pass (register-save/restore)")
            tier = "B-stackframes-real-dynamic" if dy else "D-stackframes-frame-certificate"
        elif "/flags/" in p and p in fmap:
            e = fmap[p]["equivalence"]
            ev = "%s: %d executed semantic-equivalence vectors; compile OK" % (
                e["status"], e["vectors"])
            tier, fid = "C-flags-vector-equivalence", fmap[p]["id"]
        else:
            ev, tier, fid = (
                "first-wave instruction-mapping evidence (pre-existing blob module)",
                "0-first-wave", "")
        included.append(dict(module=p, id=fid, tier=tier, evidence=ev))

    excluded = []
    for label, mp, reason in [
        ("tails", tmap,
         "every real-dynamic tails conversion is admitted; none excluded for budget"),
        ("flags", fmap,
         "budget: lower executed-vector count than admitted tier-C modules; did not fit the 30920 B cap"),
        ("stackframes", smap,
         "budget: frame-certificate tier ranks below real-execution and vector-equivalence tiers; bank-3 gap exhausted"),
    ]:
        for p, v in mp.items():
            if p not in included_paths:
                excluded.append(dict(
                    wave=label, module=p,
                    id=(v["id"] if "id" in v else v[0]), reason=reason))
    for name, raw in [("drive_all_key_lines", "0:A162"), ("zero_key_lines", "0:A16A")]:
        excluded.append(dict(
            wave="bank0", module="cwiz/idiomatic/bank0/%s.cwiz" % name, id=raw,
            reason=("cross-batch duplicate definition filtered from the generated source "
                    "(cwiz trees not edited); authoritative definition is "
                    "bank3key/key_runtime.cwiz in the boot payload (same raw id, int-return "
                    "ABI, 154/154 dynamic key-primitive evidence); bank0 copy is static-only void")))
    for m in sweep.get("mismatch_analysis", []):
        excluded.append(dict(
            wave=m.get("batch"), module=m.get("function_id"), id=m.get("function_id"),
            reason="sweep mismatch (%s / %s) not claimed: %s" % (
                m.get("status"), m.get("verdict"), m.get("interpretation", ""))))
    for k in ["f_1B818", "f_1B83A", "f_1B84E"]:
        excluded.append(dict(
            wave="stackframes", module=sf["functions"][k].get("module", ""), id=k,
            reason=("dynamic-verification-inconclusive: structural reference never reached "
                    "the 0xA5 harness end; demoted to blocked upstream")))

    program = build / "cwiz-os-program.bin"
    rom = build / "cwiz-os-integrated.rom"
    combined = build / "recovered-combined-blob.bin"
    rom_bytes = rom.read_bytes() if rom.exists() else b""
    checksum_lines = []
    vtxt = build / "final-evidence/packager-verify.txt"
    if vtxt.exists():
        checksum_lines = [
            ln.strip() for ln in vtxt.read_text(encoding="utf-8").splitlines()
            if "checksum" in ln.lower()]
    emulator = {}
    old = build / "final-manifest.json"
    if old.exists():
        try:
            emulator = rd(old).get("emulator", {})
        except Exception:
            emulator = {}

    manifest = dict(
        schema="cwiz-os-final-port/1",
        generated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        generator="cwiz_os/apps/recovered/gen_final_manifest.py via make final-manifest",
        policy=dict(
            order=[
                "A: tails real wizrun differential (43/43 vectors)",
                "B: stackframes real wizrun differential (3/3 pairs each)",
                "C: flags executed semantic-equivalence vectors, descending count then path",
                "D: stackframes frame certificates only (budget exhausted)",
            ],
            within_tier="lexicographic module path (flags: executed-vector count desc, then path)",
            fit=("admit while cumulative compiled idiomatic blob <= IDIOMATIC_SIZE_CAP "
                 "30920 B (64 B reserve under the 30984 B absolute ceiling)"),
            reject="sweep mismatch or inconclusive dynamic verification is never admitted",
            dedup=("bank0 drive_all_key_lines/zero_key_lines filtered from generated source; "
                   "authoritative bank3key/key_runtime.cwiz in boot payload"),
        ),
        bank3_gap=dict(start="0x30E40", end="0x3FFC0", bytes=61824),
        payload=dict(path=str(program), bytes=size(program), budget=31502,
                     headroom=(31502 - size(program)) if size(program) is not None else None),
        blobs=dict(
            recovered=dict(bytes=size(build / "recovered-blob.bin"), base="0x30E40"),
            idiomatic=dict(bytes=size(build / "idiomatic-blob.bin"),
                           base="0x30E40 + recovered size"),
            combined=dict(bytes=size(combined),
                          unused_gap=(61824 - size(combined)) if size(combined) is not None else None)),
        rom=dict(path=str(rom), size=size(rom),
                 sha256=hashlib.sha256(rom_bytes).hexdigest().upper() if rom_bytes else None,
                 checksum_lines=checksum_lines),
        counts=dict(
            included_total=len(included),
            included_by_tier={t: sum(1 for x in included if x["tier"] == t)
                              for t in sorted({x["tier"] for x in included})},
            excluded_total=len(excluded),
            waves=dict(
                tails=len(tails["converted"]), flags=len(flags["converted"]),
                stackframes=len([v for v in sf["functions"].values()
                                 if v.get("status") == "converted"]),
                block_counts=dict(
                    tails=len(tails["blocked"]), flags=len(flags["blocked"]),
                    stackframes=len([v for v in sf["functions"].values()
                                     if v.get("status") == "blocked"])),
            )),
        included=included,
        excluded=excluded,
        emulator=emulator,
    )
    with open(build / "final-manifest.json", "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    print("final-manifest.json: %d included, %d excluded, ROM %s"
          % (len(included), len(excluded), manifest["rom"]["sha256"]))


if __name__ == "__main__":
    main()
