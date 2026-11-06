// test_cwmode.cpp - regression tests for the cwmode firmware patcher.
//
// The fixtures under tests/data/cwmode were produced by the reference C#
// patchers in tools/cwmode/reference, using a small WPF BitmapImage shim.  The
// SHA-256 of the complete patched image therefore proves byte-for-byte
// compatibility with the original tools.  The ROMs themselves stay in
// emulator/models and are only read.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "cwmode.hpp"
#include "harness.hpp"
#include "sha256.hpp"

namespace {

namespace fs = std::filesystem;

std::string data_file(const std::string& name) {
    return wt::root_path("tests/data/cwmode/" + name);
}

std::map<std::string, std::string> read_kv(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t sp = line.find(' ');
        if (sp == std::string::npos) continue;
        out[line.substr(0, sp)] = line.substr(sp + 1);
    }
    return out;
}

uint32_t u24(const std::vector<uint8_t>& rom, uint32_t at) {
    return (uint32_t)rom[at] | ((uint32_t)rom[at + 1] << 8) | ((uint32_t)rom[at + 2] << 16);
}

uint32_t u16(const std::vector<uint8_t>& rom, uint32_t at) {
    return (uint32_t)rom[at] | ((uint32_t)rom[at + 1] << 8);
}

// Find a 6-byte 580 mode-table record: 3-byte string pointer, pad, mode code,
// pad.  Both the simple and the extended layout append these records.
bool find_mode_record(const std::vector<uint8_t>& rom, uint32_t begin, uint32_t end,
                      uint32_t name_addr, uint8_t code) {
    for (uint32_t i = begin; i + 6 <= end; ++i) {
        if (u24(rom, i) == name_addr && rom[i + 3] == 0 && rom[i + 4] == code &&
            rom[i + 5] == 0)
            return true;
    }
    return false;
}

bool load(const std::string& name, wiz::cwmode::Image& image) {
    std::string error;
    return wiz::cwmode::load_image(data_file(name), image, error);
}

wiz::cwmode::ModeSpec mode580(uint8_t code, const char* icon, const char* n0, const char* n1,
                              uint32_t entry) {
    wiz::cwmode::ModeSpec m;
    m.code = code;
    m.entry = entry;
    load(icon, m.icon);
    m.names = {n0, n1};
    return m;
}

wiz::cwmode::ModeSpec mode880(uint8_t code, const char* icon, const char* t0, const char* t1,
                              uint32_t entry) {
    wiz::cwmode::ModeSpec m;
    m.code = code;
    m.entry = entry;
    load(icon, m.icon);
    wiz::cwmode::Image a, b;
    load(t0, a);
    load(t1, b);
    m.text_images = {std::move(a), std::move(b)};
    return m;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec);
}

}  // namespace

namespace wt {

void run_cwmode() {
    using namespace wiz::cwmode;
    const std::string p580 = wt::root_path("emulator/models/fx-580VN X/rom.bin");
    const std::string p880 = wt::root_path("emulator/models/fx-880BTG/rom.bin");
    const bool have580 = file_exists(p580);
    const bool have880 = file_exists(p880);
    if (!have580 && !have880) {
        // The model dumps are not part of a source checkout.  Skip rather than
        // fail: the patcher itself is covered by the golden hashes as soon as
        // a ROM is present.
        std::printf("NOTE cwmode: no model ROMs under emulator/models; skipping the fixture\n");
        return;
    }

    // ------------------------------------------------------------------ 580
    if (have580) {
        bool ok = false;
        std::vector<uint8_t> rom = wiz::read_binary_file(p580, &ok);
        check("cwmode 580: ROM read", ok && rom.size() == 0x40000);

        Analysis a = analyze(rom, Model::Cw580);
        check("cwmode 580: analysis succeeds", a.ok, a.error);
        check("cwmode 580: mode count is 11", a.mode_count == 11, std::to_string(a.mode_count));
        check("cwmode 580: main hook 2:9448", a.main_hook == 0x29448, ss(a.main_hook));
        check("cwmode 580: menu hook 2:BA08", a.menu_hook == 0x2BA08, ss(a.menu_hook));
        check("cwmode 580: mode table 7FFA", a.sys_mode_table == 0x7FFA, ss(a.sys_mode_table));
        check("cwmode 580: string table 7FFC", a.sys_str_table == 0x7FFC, ss(a.sys_str_table));
        check("cwmode 580: code end 3:0E42", a.code_end == 0x30E42, ss(a.code_end));
        check("cwmode 580: comp func 2E234", a.comp_func == 0x2E234, ss(a.comp_func));

        const std::map<std::string, std::string> g = read_kv(data_file("golden_580_simple.txt"));
        if (g.count("input_sha256") && g.count("output_sha256")) {
            check("cwmode 580: fixture matches this ROM revision",
                  wt::sha256_hex(rom) == g.at("input_sha256"));

            Options o;
            o.model = Model::Cw580;
            std::vector<ModeSpec> modes = {
                mode580(0xE1, "icon_580_a.png", "EXTRA1", "EXTRA1B", 0x31000),
                mode580(0xE2, "icon_580_b.png", "*414243", "*4445", 0x31200)};
            PatchResult r = patch(rom, modes, o);
            check("cwmode 580 simple: patch succeeds", r.ok, r.error);
            if (r.ok) {
                check("cwmode 580 simple: byte-for-byte equal to the C# tool",
                      wt::sha256_hex(r.rom) == g.at("output_sha256"), wt::sha256_hex(r.rom));
                check("cwmode 580 simple: target is 3:0E42", r.target == 0x30E42, ss(r.target));
                check("cwmode 580 simple: mode count becomes 13",
                      r.rom[a.mode_num_addr] == 13 && r.mode_count_after == 13);
                check("cwmode 580 simple: first icon at the target",
                      r.icon_addr.size() == 2 && r.icon_addr[0] == r.target);
                check("cwmode 580 simple: icon table entries point at the icons",
                      u24(r.rom, r.icon_table + 4 * 11 + 4) == r.icon_addr[0] &&
                          u24(r.rom, r.icon_table + 4 * 11 + 8) == r.icon_addr[1]);
                // The 580 builds a fresh per-language string table; its two
                // 16-bit language pointers live at result.str_table.
                // Only the low 16 bits are stored; the text extractor takes
                // the high byte from DSR (the str_table segment).
                const uint32_t pp0 = (r.str_table & 0xFFFF0000u) | u16(r.rom, r.str_table);
                const uint32_t pp1 =
                    (r.str_table & 0xFFFF0000u) | u16(r.rom, r.str_table + 2);
                check("cwmode 580 simple: language table pointers",
                      pp0 >= r.target && pp0 < r.final_cursor && pp1 >= r.target &&
                          pp1 < r.final_cursor,
                      ss(pp0) + " / " + ss(pp1));
                check("cwmode 580 simple: mode records exist",
                      find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[0], 0xE1) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[1], 0xE2) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[2], 0) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[3], 0));
                check("cwmode 580 simple: string pointers are inside the appended block",
                      r.name_addr[0] >= r.target && r.name_addr[3] < r.final_cursor);
                check("cwmode 580 simple: changed spans are reported",
                      !r.changed.empty() && r.changed.front().first == a.mode_num_addr);
            }
        }

        const std::map<std::string, std::string> ge = read_kv(data_file("golden_580_ext.txt"));
        if (ge.count("output_sha256")) {
            Options o;
            o.model = Model::Cw580;
            std::vector<ModeSpec> modes = {
                mode580(0xE1, "icon_580_a.png", "EXTRA1", "EXTRA1B", 0x31000),
                mode580(0xE2, "icon_580_b.png", "*414243", "*4445", 0x31200),
                mode580(0xE3, "icon_580_c.png", "THREE", "*5A", 0x31400),
                mode580(0xE4, "icon_580_d.png", "FOUR", "*3132", 0x31600)};
            PatchResult r = patch(rom, modes, o);
            check("cwmode 580 extended: patch succeeds", r.ok, r.error);
            if (r.ok) {
                check("cwmode 580 extended: byte-for-byte equal to the C# tool",
                      wt::sha256_hex(r.rom) == ge.at("output_sha256"), wt::sha256_hex(r.rom));
                check("cwmode 580 extended: mode count becomes 15",
                      r.rom[a.mode_num_addr] == 15);
                check("cwmode 580 extended: string extractor hook is a B",
                      r.rom[a.menu_hook + 0xF4] == 0x00);
                const uint32_t extract = (uint32_t)((r.rom[a.menu_hook + 0xF5] & 0x0F) << 16) |
                                         r.rom[a.menu_hook + 0xF6] |
                                         ((uint32_t)r.rom[a.menu_hook + 0xF7] << 8);
                check("cwmode 580 extended: extractor hook points into the appended block",
                      extract >= r.target && extract < r.final_cursor, ss(extract));
                check("cwmode 580 extended: string table is appended",
                      r.str_table >= r.target && r.str_table < r.final_cursor, ss(r.str_table));
                check("cwmode 580 extended: all eight mode records exist",
                      find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[0], 0xE1) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[1], 0xE2) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[2], 0xE3) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[3], 0xE4) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[4], 0) &&
                          find_mode_record(r.rom, r.target, r.final_cursor, r.name_addr[7], 0));
            }
        }

        // Validation failures must leave the image empty.
        {
            std::vector<uint8_t> short_rom(0x3FFFF, 0xFF);
            check("cwmode 580: wrong ROM size is rejected", !analyze(short_rom, Model::Cw580).ok);
            std::vector<uint8_t> bad = rom;
            bad[a.mode_num_ref] = 0x00;
            check("cwmode 580: broken signature is rejected", !analyze(bad, Model::Cw580).ok);
            check("cwmode 880: a 580 image is rejected", !analyze(rom, Model::Cw880).ok);
        }
        {
            Options o;
            o.model = Model::Cw580;
            o.at = 0x20000;  // inside the firmware code
            std::vector<ModeSpec> modes = {mode580(0xE1, "icon_580_a.png", "A", "B", 0x31000)};
            PatchResult r = patch(rom, modes, o);
            check("cwmode 580: patch inside code is rejected", !r.ok && r.rom.empty());
        }
        {
            std::vector<uint8_t> dirty = rom;
            dirty[0x30F00] = 0x00;  // inside the default target range
            Options o;
            o.model = Model::Cw580;
            std::vector<ModeSpec> modes = {mode580(0xE1, "icon_580_a.png", "A", "B", 0x31000)};
            PatchResult r = patch(dirty, modes, o);
            check("cwmode 580: non-blank target is rejected", !r.ok && r.rom.empty(), r.error);
            o.force = true;
            PatchResult forced = patch(dirty, modes, o);
            check("cwmode 580: --force accepts a non-blank target", forced.ok, forced.error);
        }
        {
            Options o;
            o.model = Model::Cw580;
            o.at = 0x30E43;
            std::vector<ModeSpec> modes = {mode580(0xE1, "icon_580_a.png", "A", "B", 0x31000)};
            check("cwmode 580: odd target is rejected", !patch(rom, modes, o).ok);
        }
        {
            ModeSpec tiny;
            tiny.code = 0xE1;
            tiny.icon.width = 1;
            tiny.icon.height = 1;
            tiny.icon.rgba = {0, 0, 0, 255};
            tiny.entry = 0x31000;
            Options o;
            o.model = Model::Cw580;
            check("cwmode 580: undersized icon is rejected",
                  !patch(rom, {tiny}, o).ok);
        }
    }

    // ------------------------------------------------------------------ 880
    if (have880) {
        bool ok = false;
        std::vector<uint8_t> full = wiz::read_binary_file(p880, &ok);
        check("cwmode 880: ROM read", ok && full.size() >= 0x60000);
        std::vector<uint8_t> window(full.begin(), full.begin() + 0x60000);

        Analysis a = analyze(window, Model::Cw880);
        check("cwmode 880: analysis succeeds", a.ok, a.error);
        check("cwmode 880: mode count is 12", a.mode_count == 12, std::to_string(a.mode_count));
        check("cwmode 880: main hook 3:9500", a.main_hook == 0x39500, ss(a.main_hook));
        check("cwmode 880: menu hook 0:DFC4", a.menu_hook == 0xDFC4, ss(a.menu_hook));
        check("cwmode 880: mode table 6008", a.sys_mode_table == 0x6008, ss(a.sys_mode_table));
        check("cwmode 880: string table 6338", a.sys_str_table == 0x6338, ss(a.sys_str_table));
        check("cwmode 880: code end 5:4C28", a.code_end == 0x54C28, ss(a.code_end));
        check("cwmode 880: comp func 44A64", a.comp_func == 0x44A64, ss(a.comp_func));

        const std::map<std::string, std::string> g = read_kv(data_file("golden_880_simple.txt"));
        if (g.count("input_sha256") && g.count("output_sha256")) {
            check("cwmode 880: fixture matches this ROM revision",
                  wt::sha256_hex(window) == g.at("input_sha256"));

            Options o;
            o.model = Model::Cw880;
            std::vector<ModeSpec> modes = {
                mode880(0xE1, "icon_880_a.png", "text_880_a.png", "text_880_b.png", 0x30000),
                mode880(0xE2, "icon_880_b.png", "text_880_c.png", "text_880_d.png", 0x30200)};
            PatchResult r = patch(window, modes, o);
            check("cwmode 880 simple: patch succeeds", r.ok, r.error);
            if (r.ok) {
                check("cwmode 880 simple: byte-for-byte equal to the C# tool",
                      wt::sha256_hex(r.rom) == g.at("output_sha256"), wt::sha256_hex(r.rom));
                check("cwmode 880 simple: target is 5:4C28", r.target == 0x54C28, ss(r.target));
                check("cwmode 880 simple: mode count becomes 14",
                      r.rom[a.mode_num_addr] == 14 && r.mode_count_after == 14);
                check("cwmode 880 simple: first icon at the target",
                      r.icon_addr.size() == 2 && r.icon_addr[0] == r.target);
                const uint32_t table = r.icon_table + 8 * 12 + 8;
                check("cwmode 880 simple: icon table has both planes and the mode code",
                      u24(r.rom, table) == r.icon_addr[0] &&
                          u24(r.rom, table + 4) == r.icon_addr[0] + 0x80 && r.rom[table + 7] == 0xE1,
                      ss(u24(r.rom, table)) + " / " + ss(u24(r.rom, table + 4)));
                const uint32_t slot0 = a.sys_str_table + (12 + 1) * 4;
                const uint32_t slot1 = a.sys_str_table + (12 + 2) * 4;
                check("cwmode 880 simple: language 0 string pointers",
                      u24(r.rom, slot0) == r.name_addr[0] && u24(r.rom, slot1) == r.name_addr[1],
                      ss(u24(r.rom, slot0)) + " / " + ss(u24(r.rom, slot1)));
                check("cwmode 880 simple: language 1 string pointers",
                      u24(r.rom, slot0 + 60) == r.name_addr[2] &&
                          u24(r.rom, slot1 + 60) == r.name_addr[3]);
            }

            // A full 1 MB dump keeps its tail and patches the same window.
            if (full.size() > 0x60000) {
                PatchResult big = patch(full, modes, o);
                check("cwmode 880 1MB: patch succeeds", big.ok, big.error);
                if (big.ok) {
                    const std::string prefix =
                        wt::sha256_hex(big.rom.data(), 0x60000);
                    check("cwmode 880 1MB: code window equals the reference output",
                          prefix == g.at("output_sha256"), prefix);
                    check("cwmode 880 1MB: bytes past 0x60000 are untouched",
                          std::equal(big.rom.begin() + 0x60000, big.rom.end(),
                                     full.begin() + 0x60000));
                }
            }
        }

        const std::map<std::string, std::string> ge = read_kv(data_file("golden_880_ext.txt"));
        if (ge.count("output_sha256")) {
            Options o;
            o.model = Model::Cw880;
            std::vector<ModeSpec> modes = {
                mode880(0xE1, "icon_880_a.png", "text_880_a.png", "text_880_b.png", 0x30000),
                mode880(0xE2, "icon_880_b.png", "text_880_c.png", "text_880_d.png", 0x30200),
                mode880(0xE3, "icon_880_c.png", "text_880_e.png", "text_880_f.png", 0x30400)};
            PatchResult r = patch(window, modes, o);
            check("cwmode 880 extended: patch succeeds", r.ok, r.error);
            if (r.ok) {
                check("cwmode 880 extended: byte-for-byte equal to the C# tool",
                      wt::sha256_hex(r.rom) == ge.at("output_sha256"), wt::sha256_hex(r.rom));
                check("cwmode 880 extended: mode count becomes 15",
                      r.rom[a.mode_num_addr] == 15);
                check("cwmode 880 extended: text hooks are BLs",
                      r.rom[a.menu_hook + 0x92] == 0x01 &&
                          r.rom[a.menu_hook + 0xEE] == 0x01);
                check("cwmode 880 extended: extended string table is appended",
                      r.str_table >= r.target && r.str_table < r.final_cursor, ss(r.str_table));
            }
        }

        // The 880 can also be told to write at logical 0x70000, which is the
        // last 8 KB of the code window (physical 0x5E000).
        {
            Options o;
            o.model = Model::Cw880;
            o.at = 0x70000;
            std::vector<ModeSpec> modes = {
                mode880(0xE3, "icon_880_c.png", "text_880_e.png", "text_880_f.png", 0x30400)};
            PatchResult r = patch(full, modes, o);
            check("cwmode 880: logical 0x70000 is relocated to 0x5E000",
                  r.ok && r.target == 0x5E000, r.error);
            if (r.ok) {
                const uint32_t chain = (uint32_t)((r.rom[a.menu_hook + 1] & 0x0F) << 16) |
                                       r.rom[a.menu_hook + 2] |
                                       ((uint32_t)r.rom[a.menu_hook + 3] << 8);
                check("cwmode 880: relocated hook stores a 0x7xxxx pointer",
                      chain >= 0x70000 && chain < 0x72000, ss(chain));
                const uint32_t table = r.icon_table + 8 * 12 + 8;
                const uint32_t p1 = u24(r.rom, table);
                check("cwmode 880: relocated icon table stores a 0x7xxxx pointer",
                      p1 >= 0x70000 && p1 < 0x72000, ss(p1));
            }
        }
    }
}

}  // namespace wt
