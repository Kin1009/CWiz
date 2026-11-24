ROOT := $(shell git -C "$(CURDIR)" rev-parse --show-toplevel)
PROJECT := $(CURDIR)
BUILD_DIR := $(PROJECT)/build
MODEL_CODE ?= 0580
ifneq ($(MODEL_CODE),0580)
ifneq ($(strip $(MAKECMDGOALS)),serial)
$(error this standalone ROM project targets fx-580 (0580); use `make serial MODEL_CODE=0880` only to generate another model's serial)
endif
endif
MODEL_DIR := $(BUILD_DIR)/fx-580-cwiz-os-model
SERIAL_SOURCE := $(BUILD_DIR)/serial_number.cwiz
PAYLOAD := $(BUILD_DIR)/cwiz-os-program.bin
ROM := $(BUILD_DIR)/cwiz-os-standalone.rom
SOURCE := $(PROJECT)/main.cwiz
WIZC := $(ROOT)/build/wizc
STOCK_ROM := $(ROOT)/emulator/models/fx-580VN X/rom.bin
STOCK_RAM := $(ROOT)/emulator/models/fx-580VN X/ram.dmp
EMU_BUILD := $(ROOT)/emulator/build
EMU_APP := $(EMU_BUILD)/CasioEmuMsvc/CasioEmuMsvc.app

# --- integrated build: boot payload + preserved recovered-code blob ---------
FW_RE := $(ROOT)/firmware-re/fx580
SHIM_GUARD := $(FW_RE)/cwiz/shim/fw_globals_guard.cwiz
RECOVERED_SRC := $(BUILD_DIR)/recovered-blob-source.cwiz
RECOVERED_BLOB := $(BUILD_DIR)/recovered-blob.bin
INTEGRATED_ROM := $(BUILD_DIR)/cwiz-os-integrated.rom
INTEGRATED_MODEL_DIR := $(BUILD_DIR)/fx-580-cwiz-os-integrated-model
# 3:0E40 is the start of the measured 61,824-byte blank bank-3 flash gap.
BLOB_BASE ?= 0x30E40

# The converted idiomatic modules that do not fit in the boot payload are
# preserved in a second compiled image.  The Makefile concatenates the two
# images into ONE --blob for tools/package_integrated.py (which takes a single
# --blob argument); the packager is not modified.  See
# docs/idiomatic-integration.md for the byte-exact manifest and the
# relocation caveat that makes the blob a preserved artifact, not a loader.
#
# FINAL EVIDENCE-RANKED SELECTION (docs/final-evidence-ranked-integration.md):
# the bank-3 gap is fixed at 61,824 B, so this image can only hold the highest
# evidence conversions.  Ranking, strongest first:
#   1. real wizrun/vector differential equivalence (tails, real-dynamic
#      stackframes, symbolic, blflags, loops);
#   2. exhaustive semantic-model vector equivalence (flags);
#   3. frame certificates only (remaining stackframes);
#   4. static instruction-mapping proof (first-wave bank0/1a/1b/2).
# Tiers 3 and 4 are too weak to displace tier 1 and are excluded with exact
# reasons in build/final-manifest.json.  The 206 first-wave modules that used
# to occupy this image were all tier 4; their structural copies remain in the
# recovered blob, and the authoritative UI key path is compiled into the boot
# payload.  No cwiz tree is edited -- the selection is a source-list filter.
IDIOMATIC_DIR := $(FW_RE)/cwiz/idiomatic
IDIOMATIC_SRC := $(BUILD_DIR)/idiomatic-blob-source.cwiz
IDIOMATIC_BLOB := $(BUILD_DIR)/idiomatic-blob.bin
COMBINED_BLOB := $(BUILD_DIR)/recovered-combined-blob.bin
# Each compiled-at-0 image must be relocated for its OWN final base before
# concatenation; relocating the concatenated image once would mis-base the
# second part.  See docs/blob-relocation.md.
RELOCATED_RECOVERED := $(BUILD_DIR)/recovered-blob.reloc.bin
RELOCATED_RECOVERED_MAP := $(BUILD_DIR)/recovered-blob.reloc.map.json
RELOCATED_IDIOMATIC := $(BUILD_DIR)/idiomatic-blob.reloc.bin
RELOCATED_IDIOMATIC_MAP := $(BUILD_DIR)/idiomatic-blob.reloc.map.json
RELOCATOR := $(PROJECT)/tools/relocate_blob.py
RELOC_VERIFIER := $(PROJECT)/tools/verify_blob_relocation.py

# --- space recovery: deduplicate structural copies of converted functions -----
# The recovered blob compiled every linked/deep module.  38 of those modules
# define functions that ALSO have a curated idiomatic port in the idiomatic
# blob (and, for the four tails modules, a real wizrun differential).  Keeping
# both copies wasted 4,512 B of the 61,824-B bank-3 gap.  These are the LOWER-
# evidence structural copies, so they are the ones dropped; the idiomatic ports
# remain.  A compile check confirms no other linked/deep module calls them.
# No cwiz tree is edited: the filter is applied only to the generated source.
# See docs/space-recovery.md for the byte-exact before/after and the tradeoffs.
RECOVERED_DUP_EXCLUDE := \
	fw_09B90.cwiz fw_0A9DE.cwiz fw_0AC60.cwiz fw_0AF1A.cwiz \
	fw_0BF12.cwiz fw_0BF22.cwiz fw_0C7B0.cwiz fw_0E5E6.cwiz \
	fw_0EA4A.cwiz fw_0ED88.cwiz fw_13AB4.cwiz fw_13ABC.cwiz \
	fw_13AC8.cwiz fw_13ACC.cwiz fw_13AD4.cwiz fw_13B5A.cwiz \
	fw_13B6E.cwiz fw_13B86.cwiz fw_13B96.cwiz fw_143D6.cwiz \
	fw_1443A.cwiz fw_14442.cwiz fw_14448.cwiz fw_14450.cwiz \
	fw_14458.cwiz fw_14460.cwiz fw_14468.cwiz fw_14470.cwiz \
	fw_14476.cwiz fw_14BCC.cwiz fw_153A6.cwiz fw_153B2.cwiz \
	fw_153BC.cwiz fw_153CE.cwiz fw_17026.cwiz fw_17030.cwiz \
	fw_17B16.cwiz fw_17B28.cwiz

# The former B0_EXCLUDE / B2_IDIOMATIC first-wave source lists were retired by
# the evidence-ranked selection: all 206 bank0/bank1a/bank1b/bank2 modules are
# tier-4 static proof and are excluded.  Their structural copies remain in the
# recovered blob.  The two cross-batch duplicate key-line definitions were
# already filtered from the recovered source by RECOVERED_DUP_EXCLUDE, and the
# authoritative bank3key/key_runtime.cwiz leaves (raw 0:A162 / 0:A16A,
# 154/154 dynamic key-primitive evidence) are compiled INTO the boot payload
# by apps/recovered/recovered.cwiz.  No cwiz tree is edited.

# --- final evidence-ranked selection (details: docs/final-evidence-ranked-integration.md)
# Deterministic policy (recorded in build/final-manifest.json): highest evidence
# tier first, then within a tier by descending executed-vector count and
# ascending compiled byte cost.  A module is admitted only while the cumulative
# compiled idiomatic blob stays at or under IDIOMATIC_SIZE_CAP (below).  Modules
# with sweep mismatches or inconclusive dynamic verification are never admitted.
# The whole tier-4 first-wave set (bank0/1a/1b/2, 206 modules) and tier-3
# frame-certificate-only stackframes (163 modules) are excluded; every exclusion
# with its exact reason and byte cost is in build/final-manifest.json.
#
# Tier A1: shared-tail / goto wave - real wizrun differential, 43/43 vectors.
TAILS_IDIOMATIC := \
	buffer_backscan_high_byte.cwiz firmware_char_decode_16.cwiz \
	firmware_string_cell_count.cwiz math_0C1D0.cwiz math_16DA2.cwiz \
	mode_dependent_char_code.cwiz system_helper_0CB0E.cwiz ui_0C784.cwiz \
	ui_mode_code_resolver.cwiz xbank_helper_0D80C.cwiz
# Tier A2: stackframes with a real wizrun differential - 3/3 pairs each.
SF_DYNAMIC_IDIOMATIC := \
	display_0AAD4.cwiz math_19F7E.cwiz math_1B4AC.cwiz ui_1B708.cwiz \
	ui_1B71C.cwiz bcd_17_access_2B8DE.cwiz
# Tier A3 (NEW): symbolic-address / external-tail ports, 46/46 real wizrun
# vectors after the three documented bug fixes.  The two largest modules
# (ui_2D314: 15,130 B, ui_28350: 7,236 B) are excluded by the byte budget;
# including them would displace 20+ smaller real-dynamic modules.  The shared
# _symbolic_boundary.cwiz ABI stub is included once (it has an include guard).
SYMBOLIC_IDIOMATIC := \
	bcd_sign_dispatch_17B80.cwiz common_helper_16D6C.cwiz \
	common_helper_16D80.cwiz common_helper_17076.cwiz \
	common_helper_1708C.cwiz common_user_17082.cwiz display_helper_16790.cwiz \
	math_15396.cwiz math_1571E.cwiz math_2729C.cwiz math_2F62E.cwiz \
	mode_access_16D48.cwiz ui_10674.cwiz \
	ui_block_13342_helper_10490.cwiz ui_block_13342_helper_13336.cwiz \
	ui_block_16ECE_helper_161C8.cwiz ui_block_17BDE_helper_17CA6.cwiz \
	ui_input_0E4AE.cwiz ui_input_2D11C.cwiz ui_tail_cmp_106A0.cwiz \
	xbank_helper_15E36.cwiz xbank_helper_16FD6.cwiz
SYMBOLIC_BOUNDARY := _symbolic_boundary.cwiz
# Tier A4 (NEW): BL-flag / banked-CSR / PSW blockers - 52/52 real verify-io
# vectors, no flag model.
BLFLAGS_IDIOMATIC := \
	char_range_predicate_09ADA.cwiz compare_key_16DFA.cwiz \
	interrupts_disable.cwiz interrupts_enable.cwiz \
	keycode_length_2ABE4.cwiz ui_status_classifier_17096.cwiz
# Tier A5 (NEW): loop / back-edge blockers - 44/44 real wizrun vectors and a
# passing static frame proof each.
LOOPS_IDIOMATIC := \
	count_empty_cells_19FB4.cwiz count_nibble_cells_1AB82.cwiz \
	doubling_count_161E2.cwiz first_table_match_2E2BC.cwiz \
	second_table_match_1FBF6.cwiz strcpy_0E5C8.cwiz \
	ui_state_predicate_0AF2C.cwiz
# Tier B: exhaustive semantic-model vector equivalence (flags), ranked by
# descending vector count then ascending marginal byte cost.  Only the five
# highest-evidence modules that fit the remaining 948 B are admitted;
# hub_1A9FC (8,866 vectors, 10,728 B) and all other flags stay excluded.
FLAGS_IDIOMATIC := \
	ui_block_17B80_caller_17B48.cwiz math_15374.cwiz math_15722.cwiz \
	math_15730.cwiz
#
# After dedup the recovered blob is 26,328 B, so the idiomatic ceiling is
# 61,824 - 26,328 = 35,496 B.  This cap stops 64 B short of it (35,432 B).
# The selected 55 modules plus the boundary stub compile to about 35,076 B,
# leaving about 420 B of the bank-3 gap free.  The lowest-evidence tier-B flag
# (ui_14258, 6,571 vectors) was dropped after the Wiz PRELUDE port grew shared
# helpers; the four 8,088-vector flags remain.  NOTE: the read-only
# gen_final_manifest.py
# still prints the old 30,920/30,984 numbers in its policy.fit string; the
# authoritative sizes and per-module evidence are in blobs/counts/included.
IDIOMATIC_SIZE_CAP := 35432

.PHONY: all compiler serial build emulator run clean verify-ram ui-demo \
	integrated blob-source idiomatic-blob-source verify-integrated verify-relocation \
	final-manifest final-evidence-manifest

all: run

compiler:
	cmake -S "$(ROOT)/compiler" -B "$(ROOT)/build"
	cmake --build "$(ROOT)/build" --target wizc --parallel 2

VERSION_SOURCE := $(BUILD_DIR)/version.cwiz

serial:
	python3 "$(PROJECT)/tools/generate_serial.py" \
		--project "$(PROJECT)" --output "$(SERIAL_SOURCE)" --model "$(MODEL_CODE)"

version:
	python3 "$(PROJECT)/tools/gen_version.py"

icons:
	python3 "$(PROJECT)/tools/build_menu_bitmaps.py"

.PHONY: rom-hash
rom-hash: build
	python3 -c 'import hashlib,pathlib; p=pathlib.Path("$(ROM)"); print("ROM SHA-256:", hashlib.sha256(p.read_bytes()).hexdigest().upper())'

ui-demo: compiler
	mkdir -p "$(BUILD_DIR)"
	"$(WIZC)" --target cw --lang cwiz -o "$(BUILD_DIR)/chooser-demo.bin" "$(PROJECT)/ui/demo.cwiz"

build: icons serial version compiler
	mkdir -p "$(BUILD_DIR)"
	"$(WIZC)" --target cw --lang cwiz -o "$(PAYLOAD)" "$(SOURCE)"
	python3 "$(PROJECT)/tools/package_standalone.py" \
		--program "$(PAYLOAD)" \
		--base-rom "$(STOCK_ROM)" \
		--rom-out "$(ROM)" \
		--model-template "$(PROJECT)/assets/model" \
		--model-out "$(MODEL_DIR)" \
		--ram-template "$(STOCK_RAM)"
	@$(MAKE) --no-print-directory verify-ram

verify-ram: compiler
	@python3 "$(PROJECT)/tools/verify_ram_layout.py" --source "$(SOURCE)" --compiler "$(WIZC)"

# Generate the recovered-blob compilation root: the RAM shim guard first,
# then every curated linked module and every deep module, with a thin main().
# `sort` makes the include order deterministic; #pragma once de-duplicates
# the shared _linked_api.cwiz header.
blob-source:
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' \
		'// Generated by the cwiz_os Makefile for `make integrated`.' \
		'// Recovered firmware routines -- compiled at 0, placed at $(BLOB_BASE).' \
		'#include "$(SHIM_GUARD)"' > "$(RECOVERED_SRC)"
	@excl='$(RECOVERED_DUP_EXCLUDE)'; \
		find "$(FW_RE)/cwiz/linked" "$(FW_RE)/cwiz/deep" -name '*.cwiz' -print \
		| LC_ALL=C sort \
		| while read -r f; do \
			case " $$excl " in *" $$(basename "$$f") "*) ;; \
				*) printf '#include "%s"\n' "$$f" ;; \
			esac; \
		done >> "$(RECOVERED_SRC)"
	@printf '%s\n' \
		'void main() {' \
		'    int probe = display_metrics_table_lookup();' \
		'    probe = probe & 0xFFFF;' \
		'    while (1) { }' \
		'}' >> "$(RECOVERED_SRC)"

# Generate the evidence-ranked idiomatic compilation root.  The UI-critical
# bank-3 key path is compiled INTO the payload by recovered.cwiz; the blob
# keeps only the highest-evidence conversions that fit the bank-3 gap: real
# wizrun/vector differentials (tails, real-dynamic stackframes, symbolic,
# blflags, loops) followed by the highest-vector semantic-model flags.  The
# tier-4 first-wave bulk and tier-3 frame-certificate-only stackframes are
# deliberately absent; see docs/final-evidence-ranked-integration.md and the
# excluded[] list in build/final-manifest.json.  bank3key is also deliberately
# not included (key_runtime.cwiz redefines the bank-0 SFR leaves; those leaves
# run in the boot payload).  Explicit lists keep the manifest reproducible.
# The symbolic boundary stub has an include guard and is emitted once, ahead
# of the symbolic modules, so the 14 external-tail ABI names are defined once.
idiomatic-blob-source:
	@mkdir -p "$(BUILD_DIR)"
	@printf '%s\n' \
		'// Generated by the cwiz_os Makefile for `make integrated`.' \
		'// Evidence-ranked idiomatic CWiz modules -- compiled at 0, after the recovered blob.' \
		> "$(IDIOMATIC_SRC)"
	@for m in $(TAILS_IDIOMATIC); do \
		printf '#include "%s/tails/%s"\n' "$(IDIOMATIC_DIR)" "$$m"; \
		done >> "$(IDIOMATIC_SRC)"
	@for m in $(SF_DYNAMIC_IDIOMATIC); do \
		printf '#include "%s/stackframes/%s"\n' "$(IDIOMATIC_DIR)" "$$m"; \
		done >> "$(IDIOMATIC_SRC)"
	@printf '#include "%s/symbolic/%s"\n' "$(IDIOMATIC_DIR)" "$(SYMBOLIC_BOUNDARY)" \
		>> "$(IDIOMATIC_SRC)"
	@for m in $(SYMBOLIC_IDIOMATIC); do \
		printf '#include "%s/symbolic/%s"\n' "$(IDIOMATIC_DIR)" "$$m"; \
		done >> "$(IDIOMATIC_SRC)"
	@for m in $(BLFLAGS_IDIOMATIC); do \
		printf '#include "%s/blflags/%s"\n' "$(IDIOMATIC_DIR)" "$$m"; \
		done >> "$(IDIOMATIC_SRC)"
	@for m in $(LOOPS_IDIOMATIC); do \
		printf '#include "%s/loops/%s"\n' "$(IDIOMATIC_DIR)" "$$m"; \
		done >> "$(IDIOMATIC_SRC)"
	@for m in $(FLAGS_IDIOMATIC); do \
		printf '#include "%s/flags/%s"\n' "$(IDIOMATIC_DIR)" "$$m"; \
		done >> "$(IDIOMATIC_SRC)"
	@printf '%s\n' \
		'int idiomatic_blob_probe;' \
		'void main() {' \
		'    idiomatic_blob_probe = 0;' \
		'    while (1) { }' \
		'}' >> "$(IDIOMATIC_SRC)"

# The proper-port build: OS payload, two preserved code images concatenated
# into the single --blob the unmodified packager expects, then full
# ROM/placement verification.  `make build` is unchanged.
integrated: icons serial version compiler blob-source idiomatic-blob-source
	mkdir -p "$(BUILD_DIR)"
	"$(WIZC)" --target cw --lang cwiz -o "$(PAYLOAD)" "$(SOURCE)"
	"$(WIZC)" --target cw --lang cwiz -o "$(RECOVERED_BLOB)" "$(RECOVERED_SRC)"
	"$(WIZC)" --target cw --lang cwiz -o "$(IDIOMATIC_BLOB)" "$(IDIOMATIC_SRC)"
	python3 "$(RELOCATOR)" --blob "$(RECOVERED_BLOB)" --blob-base $(BLOB_BASE) \
		--out "$(RELOCATED_RECOVERED)" --map-out "$(RELOCATED_RECOVERED_MAP)"
	@IDIOMATIC_BASE=$$(( $(BLOB_BASE) + $$(wc -c < "$(RELOCATED_RECOVERED)" | tr -d ' ') )); \
		printf 'Idiomatic blob base: 0x%X\n' $$IDIOMATIC_BASE; \
		python3 "$(RELOCATOR)" --blob "$(IDIOMATIC_BLOB)" --blob-base $$IDIOMATIC_BASE \
			--out "$(RELOCATED_IDIOMATIC)" --map-out "$(RELOCATED_IDIOMATIC_MAP)"
	cat "$(RELOCATED_RECOVERED)" "$(RELOCATED_IDIOMATIC)" > "$(COMBINED_BLOB)"
	@$(MAKE) --no-print-directory verify-relocation
	@printf 'Blob manifest: 0x%X = relocated recovered linked/deep (%s B), 0x%X = relocated idiomatic (%s B), total %s B\n' \
		$(BLOB_BASE) "$$(wc -c < "$(RELOCATED_RECOVERED)" | tr -d ' ')" \
		$$(( $(BLOB_BASE) + $$(wc -c < "$(RELOCATED_RECOVERED)" | tr -d ' ') )) \
		"$$(wc -c < "$(RELOCATED_IDIOMATIC)" | tr -d ' ')" \
		"$$(wc -c < "$(COMBINED_BLOB)" | tr -d ' ')"
	python3 "$(PROJECT)/tools/package_integrated.py" \
		--program "$(PAYLOAD)" \
		--blob "$(COMBINED_BLOB)" \
		--blob-base "$(BLOB_BASE)" \
		--base-rom "$(STOCK_ROM)" \
		--rom-out "$(INTEGRATED_ROM)" \
		--model-template "$(PROJECT)/assets/model" \
		--model-out "$(INTEGRATED_MODEL_DIR)" \
		--ram-template "$(STOCK_RAM)"
	@$(MAKE) --no-print-directory verify-integrated
	@$(MAKE) --no-print-directory verify-ram
	@$(MAKE) --no-print-directory final-manifest
	@$(MAKE) --no-print-directory final-evidence-manifest

verify-relocation: compiler
	@python3 "$(RELOC_VERIFIER)" --blob "$(RECOVERED_BLOB)" \
		--relocated "$(RELOCATED_RECOVERED)" --map "$(RELOCATED_RECOVERED_MAP)"
	@python3 "$(RELOC_VERIFIER)" --blob "$(IDIOMATIC_BLOB)" \
		--relocated "$(RELOCATED_IDIOMATIC)" --map "$(RELOCATED_IDIOMATIC_MAP)"

verify-integrated: compiler
	@python3 "$(PROJECT)/tools/package_integrated.py" --verify \
		--program "$(PAYLOAD)" --blob "$(COMBINED_BLOB)" \
		--blob-base "$(BLOB_BASE)" --rom-out "$(INTEGRATED_ROM)"
	@python3 -c 'import hashlib,pathlib; p=pathlib.Path("$(INTEGRATED_ROM)"); \
		assert p.stat().st_size == 262144, p.stat().st_size; \
		print("Integrated ROM size:", p.stat().st_size); \
		print("Integrated ROM SHA-256:", hashlib.sha256(p.read_bytes()).hexdigest().upper())'

# Regenerate build/final-manifest.json from the ledgers and the actual build
# artifacts.  Called at the end of `make integrated`; safe to re-run after the
# headless boot because an existing `emulator` block is preserved.
final-manifest:
	@python3 "$(PROJECT)/apps/recovered/gen_final_manifest.py" "$(PROJECT)" "$(BUILD_DIR)"

# Augment the generator's coarse manifest with the evidence-ranked included and
# excluded lists, per-module compiled sizes and exact reasons.  The helper lives
# with the captured evidence so the decision is reproducible from `make
# integrated` alone; it preserves any existing emulator block when the headless
# capture has not been run yet.  See docs/final-evidence-ranked-integration.md.
final-evidence-manifest:
	@python3 "$(BUILD_DIR)/final-ranked-evidence/augment_manifest.py"

emulator:
	cmake --build "$(EMU_BUILD)" --target CasioEmuMsvc --parallel 2

run: build emulator
	open -na "$(EMU_APP)" --args "model=$(MODEL_DIR)"

clean:
	rm -rf "$(BUILD_DIR)"
