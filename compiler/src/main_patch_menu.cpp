// main_patch_menu.cpp - put a Wiz (.wz) or CWiz (.cwiz) program into the menu of
// a Casio fx-580VN X (ML620906, 256 KB) ROM.
//
//     patch_menu rom.bin app.wz|app.cwiz out.bin [--slot 2] [--force] [--ignore-hook]
//
// What it does (all addresses were read from bro.bin with a disassembler; NOT yet
// run on the real firmware):
//   1. builds the source for the 'cwapp' target: code + data at 3:0E40
//   2. writes the program there; its first bytes are a trampoline
//   3. overwrites firmware 2:5E1E "CMP R8,03 / BEQ 2:5E88" with "B 3:0E40"
//   4. sets the mode id of menu slot N to the mode the trampoline answers to
//
// The engine lives in patch.cpp so the debugger's Wiz editor can offer the same
// operation in-process.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "build.hpp"
#include "cli.hpp"
#include "common.hpp"
#include "diag.hpp"
#include "patch.hpp"

using namespace wiz;

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    set_data_dir(autodetect_data_dir(argv[0]));

    std::vector<CliOption> opts = {
        {"--slot", "slot", true}, {"--mode", "mode", true}, {"--force", "force"},
        {"--ignore-hook", "ignore_hook"},
        {"--color", "color", true, true, "always", {"auto", "always", "never"}},
        {"-h", "help"}, {"--help", "help"},
    };
    Cli a;
    std::string err;
    if (!parse_cli(argc, argv, opts, a, err)) {
        std::fprintf(stderr, "patch_menu: error: %s (try --help)\n", err.c_str());
        return 2;
    }
    if (a.has("help") || a.positional.size() < 3) {
        std::fputs(
            "patch_menu - patch a Wiz/CWiz program into the fx-580VN X menu\n"
            "\n"
            "Usage:\n"
            "    patch_menu rom.bin app.wz|app.cwiz out.bin [--slot 1..12] [--force]\n"
            "               [--ignore-hook] [--color auto|always|never]\n"
            "\n"
            "  --slot N       menu slot to replace (default: the source's #slot n, else 2)\n"
            "  --force        overwrite non-empty flash at 3:0E40\n"
            "  --ignore-hook  accept a ROM whose set_mode bytes differ from the\n"
            "                 known fx-580VN X variant (writes the hook anyway)\n"
            "\n"
            "The source language is chosen by extension: .cwiz is CWiz, anything\n"
            "else is Wiz.  The patched image is written to out.bin; the input ROM\n"
            "is never modified.\n",
            stdout);
        return a.has("help") ? 0 : 2;
    }

    set_color_mode_from_arg(a.value("color", "auto"));
    Diag g_d("patch_menu");

    const std::string rom_path = a.positional[0];
    const std::string app_path = a.positional[1];
    const std::string out_path = a.positional[2];

    bool ok = false;
    std::vector<uint8_t> rom = read_binary_file(rom_path, &ok);
    if (!ok) {
        g_d.error("cannot read ROM: " + rom_path, rom_path);
        g_d.summary();
        return 1;
    }
    std::string source = read_text_file(app_path, &ok);
    if (!ok) {
        g_d.error("cannot read " + app_path + " (expected a .wz or .cwiz source)", app_path);
        g_d.summary();
        return 1;
    }

    const int slot = a.has("slot") ? (int)std::strtol(a.value("slot").c_str(), nullptr, 10) : 0;
    if (a.has("mode") && a.value("mode") != "E0") {
        g_d.error("only the E0 menu mode is wired into template_cwapp.asm for now");
        g_d.summary();
        return 1;
    }

    RomPatchReport report = patch_rom(std::move(rom), source, app_path, slot, a.has("force"),
                                      a.has("ignore_hook"));
    for (const std::string& warning : report.warnings)
        g_d.warning(warning, rom_path);
    if (!report.ok) {
        g_d.error(report.error, app_path, report.error_line, report.error_col,
                  report.error_line > 0 ? source_line_at(source, report.error_line) : "");
        g_d.summary();
        return 1;
    }
    if (!write_binary_file(out_path, report.rom)) {
        g_d.error("cannot write " + out_path, out_path);
        g_d.summary();
        return 1;
    }

    const bool tty = stdout_is_tty();
    std::fputs(fmt("%s %s\n", bold("patch_menu", tty).c_str(), out_path.c_str()).c_str(), stdout);
    for (const std::string& change : report.changes)
        std::fputs(fmt("    %s\n", change.c_str()).c_str(), stdout);
    std::fputs(fmt("    program entry %X:%04X   exit -> 0:A1CE (firmware reset)\n",
                   (report.app_entry >> 16) & 0xFFFF, report.app_entry & 0xFFFF)
                   .c_str(),
               stdout);
    std::fputs(fmt("    slot %c keeps its icon and name; selecting it runs the program.\n",
                   "123456789ABC"[report.slot - 1])
                   .c_str(),
               stdout);
    std::fputs(fmt("    %s\n",
                   yellow("not tested on real firmware - try it in the emulator first", tty).c_str())
                   .c_str(),
               stdout);
    g_d.summary();
    return 0;
}
