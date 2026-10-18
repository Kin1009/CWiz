// main_patch_menu.cpp - put a Wiz program into the menu of a Casio fx-580VN X
// (ML620906, 256 KB) ROM.
//
//     patch_menu rom.bin app.wz out.bin [--slot 2] [--mode E0] [--force]
//
// What it does (all addresses were read from bro.bin with a disassembler; NOT yet
// run on the real firmware):
//   1. builds app.wz for the 'cwapp' target: code + data at 3:0E40
//   2. writes the program there; its first bytes are a trampoline
//   3. overwrites firmware 2:5E1E "CMP R8,03 / BEQ 2:5E88" with "B 3:0E40"
//   4. sets the mode id of menu slot N to the mode the trampoline answers to
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "build.hpp"
#include "cli.hpp"
#include "diag.hpp"

using namespace wiz;

static const int SZ = 262144;
static const int MB = 0x7FF8;  // menu table
static const int NB = 0x5516;
static const int NS = 12;
static const int HOOK = 0x25E1E;
static const uint8_t HOOK_OLD[4] = {0x03, 0xE8, 0x30, 0x68};
static const uint8_t HOOK_NEW[4] = {0x00, 0xF3, 0x40, 0x0E};  // B 3:0E40
static const int BASE = 0x30E40;
static const char* CH = "123456789ABC";

static Diag g_d("patch_menu");

static int fail() {
    g_d.summary();
    return 1;
}

static int u16(const std::vector<uint8_t>& r, int o) {
    return r[(size_t)o] | (r[(size_t)o + 1] << 8);
}

static std::string cstr(const std::vector<uint8_t>& r, int a) {
    std::string s;
    while (a < (int)r.size() && r[(size_t)a] && s.size() < 40) {
        s += (char)r[(size_t)a];
        a++;
    }
    return s;
}

static std::string hex_bytes(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) {
        if (i) s += ' ';
        s += fmt("%02x", p[i]);
    }
    return s;
}

static std::string check_menu(const std::vector<uint8_t>& r) {
    for (int k = 0; k < NS; k++) {
        int o = MB + 6 * k;
        int ip = u16(r, o), dp = u16(r, o + 4);
        if (!(0x100 <= ip && ip <= SZ - 138) || r[(size_t)o + 3] || r[(size_t)dp] != 2 ||
            r[(size_t)dp + 2] || r[(size_t)dp + 3])
            return fmt("menu entry %c does not look right", CH[k]);
        std::string s = cstr(r, u16(r, NB + 2 * (r[(size_t)dp + 1] - 2)));
        bool ok = s.size() >= 3;
        for (char c : s)
            if (!((unsigned char)c >= 32 && (unsigned char)c < 127)) ok = false;
        if (!ok) return fmt("menu entry %c: cannot read its name", CH[k]);
    }
    return "";
}

static std::string slot_name(const std::vector<uint8_t>& r, int k) {
    int o = MB + 6 * k;
    return cstr(r, u16(r, NB + 2 * (r[(size_t)u16(r, o + 4) + 1] - 2)));
}

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    set_data_dir(autodetect_data_dir(argv[0]));

    std::vector<CliOption> opts = {
        {"--slot", "slot", true}, {"--mode", "mode", true}, {"--force", "force"},
        {"--color", "color", true, true, "always", {"auto", "always", "never"}},
        {"-h", "help"}, {"--help", "help"},
    };
    Cli a;
    std::string err;
    if (!parse_cli(argc, argv, opts, a, err)) {
        std::fprintf(stderr, "patch_menu: error: %s\n", err.c_str());
        return 2;
    }
    if (a.has("help") || a.positional.size() < 3) {
        std::fputs(
            "patch_menu - patch a Wiz program into the fx-580VN X menu\n"
            "\n"
            "Usage:\n"
            "    patch_menu rom.bin app.wz out.bin [--slot 2] [--mode E0] [--force]\n",
            stdout);
        return a.has("help") ? 0 : 2;
    }

    set_color_mode_from_arg(a.value("color", "auto"));

    std::string rom_path = a.positional[0], app_path = a.positional[1], out_path = a.positional[2];

    bool ok = false;
    std::vector<uint8_t> rom = read_binary_file(rom_path, &ok);
    if (!ok) {
        g_d.error("cannot read ROM: " + rom_path, rom_path);
        return fail();
    }
    if ((int)rom.size() != SZ) {
        g_d.error(fmt("ROM is %d bytes; an fx-580VN X dump is %d", (int)rom.size(), SZ), rom_path);
        return fail();
    }
    std::string bad = check_menu(rom);
    if (!bad.empty()) {
        g_d.error("this does not look like the ROM these addresses were read from: " + bad,
                  rom_path);
        return fail();
    }
    if (std::memcmp(&rom[(size_t)HOOK], HOOK_OLD, 4) != 0) {
        g_d.error(fmt("set_mode at %05X is %s, expected %s (different firmware?)", HOOK,
                      hex_bytes(&rom[(size_t)HOOK], 4).c_str(), hex_bytes(HOOK_OLD, 4).c_str()),
                  rom_path);
        return fail();
    }

    int slot = a.has("slot") ? (int)std::strtol(a.value("slot").c_str(), nullptr, 10) : 2;
    if (!(1 <= slot && slot <= NS)) {
        g_d.error(fmt("--slot must be 1..%d", NS));
        return fail();
    }

    // Warn about slots whose mode id jumps into seg-3 stubs (0xFF) in this ROM
    const int BROKEN_MODES[] = {0x06, 0x07, 0x0C, 0x88};
    std::vector<std::string> broken;
    for (int k = 0; k < NS; k++) {
        int m = rom[(size_t)(MB + 6 * k + 2)];
        for (int b : BROKEN_MODES)
            if (b == m) broken.push_back(std::string(1, CH[k]));
    }
    if (!broken.empty())
        g_d.warning(fmt("menu slots %s use firmware modes that jump to unimplemented seg-3 "
                        "stubs in this ROM; selecting them on the real calculator will crash "
                        "(a ROM issue, not patch_menu)",
                        join(broken, ",").c_str()));

    int mode = (int)std::strtol(a.value("mode", "E0").c_str(), nullptr, 16);
    if (!(0x80 <= mode && mode <= 0xFF) || mode == 0xC1 || mode == 0xC4 || mode == 0x88 ||
        mode == 0x89) {
        g_d.error("--mode must be 80..FF and not one the firmware already uses");
        return fail();
    }

    BuildResult br;
    try {
        bool rok = false;
        std::string text = read_text_file(app_path, &rok);
        if (!rok) {
            g_d.error("cannot read " + app_path, app_path);
            return fail();
        }
        br = build_source(text, "cwapp");
    } catch (const CompileError& e) {
        bool rok = false;
        std::string text = read_text_file(app_path, &rok);
        g_d.error(e.what(), app_path, e.line, e.col, rok ? source_line_at(text, e.line) : "");
        return fail();
    } catch (const std::exception& e) {
        g_d.error(e.what(), app_path);
        return fail();
    }

    std::vector<uint8_t> code;
    if (br.rom.size() > (size_t)BASE) code.assign(br.rom.begin() + BASE, br.rom.end());

    if (mode != 0xE0) {
        g_d.error("only --mode E0 is wired into template_cwapp.asm for now");
        return fail();
    }
    if ((long)BASE + (long)code.size() > SZ) {
        g_d.error(fmt("program is %d bytes, only %d fit", (int)code.size(), SZ - BASE), app_path);
        return fail();
    }
    if (!a.has("force")) {
        for (size_t i = 0; i < code.size(); i++)
            if (rom[(size_t)BASE + i] != 0xFF) {
                g_d.error(fmt("%05X.. is not free flash (not all FF); use --force to overwrite "
                              "anyway",
                              BASE),
                          rom_path);
                return fail();
            }
    }

    std::vector<std::pair<int, std::string>> changes;
    for (size_t i = 0; i < code.size(); i++) rom[(size_t)BASE + i] = code[i];
    changes.push_back({BASE, fmt("program (%d bytes)", (int)code.size())});
    for (int i = 0; i < 4; i++) rom[(size_t)HOOK + i] = HOOK_NEW[i];
    changes.push_back(
        {HOOK, fmt("hook  %s -> %s", hex_bytes(HOOK_OLD, 4).c_str(), hex_bytes(HOOK_NEW, 4).c_str())});
    int o = MB + 6 * (slot - 1) + 2;
    std::string old = hex_bytes(&rom[(size_t)o], 2);
    rom[(size_t)o] = (uint8_t)mode;
    rom[(size_t)o + 1] = 0;
    changes.push_back({o, fmt("menu slot %c (%s) mode id %s -> %02X 00", CH[slot - 1],
                              slot_name(rom, slot - 1).c_str(), old.c_str(), mode)});
    if (!write_binary_file(out_path, rom)) {
        g_d.error("cannot write " + out_path, out_path);
        return fail();
    }

    const bool tty = stdout_is_tty();
    auto app_it = br.syms.find("_APP");
    int appv = app_it == br.syms.end() ? 0 : app_it->second;
    std::fputs(fmt("%s %s\n", bold("patch_menu", tty).c_str(), out_path.c_str()).c_str(), stdout);
    for (const auto& ch : changes)
        std::fputs(fmt("    %05X  %s\n", ch.first, ch.second.c_str()).c_str(), stdout);
    std::fputs(fmt("    program entry %X:%04X   exit -> 0:A1CE (firmware reset)\n",
                   (appv >> 16) & 0xFFFF, appv & 0xFFFF)
                   .c_str(),
               stdout);
    std::fputs(fmt("    slot %c keeps its icon and name; selecting it runs the program.\n",
                   CH[slot - 1])
                   .c_str(),
               stdout);
    std::fputs(fmt("    %s\n",
                   yellow("not tested on real firmware - try it in the emulator first", tty).c_str())
                   .c_str(),
               stdout);
    return 0;
}
