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
EMU_BUILD := $(ROOT)/emulator/build
EMU_APP := $(EMU_BUILD)/CasioEmuMsvc/CasioEmuMsvc.app

.PHONY: all compiler serial build emulator run clean verify-ram ui-demo

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
		--model-out "$(MODEL_DIR)"
	"$(WIZC)" --target cw --lang cwiz -o "$(PAYLOAD)" "$(SOURCE)"
	python3 "$(PROJECT)/tools/package_standalone.py" \
		--program "$(PAYLOAD)" \
		--base-rom "$(STOCK_ROM)" \
		--rom-out "$(ROM)" \
		--model-template "$(PROJECT)/assets/model" \
		--model-out "$(MODEL_DIR)"
	@$(MAKE) --no-print-directory verify-ram

verify-ram:
	@python3 "$(PROJECT)/tools/verify_ram_layout.py" --source "$(SOURCE)" --compiler "$(WIZC)"

emulator:
	cmake --build "$(EMU_BUILD)" --target CasioEmuMsvc --parallel 2

run: build emulator
	open -na "$(EMU_APP)" --args "model=$(MODEL_DIR)"

clean:
	rm -rf "$(BUILD_DIR)"
