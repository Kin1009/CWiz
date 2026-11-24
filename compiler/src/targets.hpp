// targets.hpp - hardware description of each calculator + `for <target>` sections.
#pragma once

#include <optional>
#include <string>
#include <vector>
#include <utility>

#include "common.hpp"

namespace wiz {

struct Target {
    std::string name;
    std::string chip;
    std::string tmpl;      // boot template file
    int rom_limit;
    int ram_lo;
    int var_base;
    int var_limit;
    // Inclusive RAM holes which the global allocator must not occupy.
    int planes;            // LCD bitplanes
    int rom_seg = 0;       // data segment (DSR) of ROM tables; 3 for 'cwapp'
    int rows = 64;         // visible LCD rows (width is always 192)
    std::string core = "nX-U8/100";
    int row_off = 0;       // VRAM row of user y=0 (cw: 1)
    // Inclusive RAM holes which the global allocator must not occupy.
    std::vector<std::pair<int, int>> reserved_ram;
};

// C-RAM-44 / BRIEF Requirement C: immutable single-address hardware registers.
// Their addresses are fixed by hardware and must never be claimed by an
// allocator, #preserve window, ROP variable or relocation path.  F0C3's NAME is
// still OPEN (boss decision O-C10: DCL "RTCWEEK" vs prelude "DAY"); the ADDRESS
// is immutable either way.  The naming conflict / emulator MMU overlap remain
// documentation + risk-register items only — no rename or emulator change here.
inline constexpr int kImmutableSfr0C3 = 0xF0C3;
inline constexpr int kImmutableSfr0D1 = 0xF0D1;
inline constexpr int kImmutableSfrF310 = 0xF310;
inline constexpr int kImmutableSingletons[3] = {
    kImmutableSfr0C3, kImmutableSfr0D1, kImmutableSfrF310};

// True when [addr, addr + bytes - 1] covers any immutable singleton.
inline bool overlaps_immutable_sfr(int addr, int bytes) {
    if (bytes <= 0) return false;
    const int end = addr + bytes - 1;
    for (int imm : kImmutableSingletons)
        if (addr <= imm && end >= imm) return true;
    return false;
}

extern const Target TARGET_CWII;
extern const Target TARGET_CW;
extern const Target TARGET_CWAPP;

extern const std::string DEFAULT_TARGET;

std::optional<std::string> canonical(const std::string& name);
const Target& get_target(const std::string& t);
const Target& get_target(const Target& t);

struct SectionError : std::runtime_error {
    explicit SectionError(const std::string& m) : std::runtime_error(m) {}
};

std::vector<std::string> declared_targets(const std::string& src);
std::string select_sections(const std::string& src, const Target& target);

}  // namespace wiz
