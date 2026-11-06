// test_cwlink.cpp - .cwapplink project builder tests.
//
// The tests build real 256 KB ROMs from tiny cwapp sources.  Golden SHA-256
// values lock the two-app and thirteen-app layouts; the individual checks also
// prove that every appended mode entry points at its own code block.
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "builtin_rom.hpp"
#include "cpre.hpp"
#include "cwlink.hpp"
#include "harness.hpp"
#include "patch.hpp"
#include "sha256.hpp"

namespace {

namespace fs = std::filesystem;

struct TempDir {
    std::string path;
    TempDir(const std::string& name) {
        path = wt::root_path("temp/cwlink-tests/" + name + "-" + std::to_string(::getpid()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void write_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary);
    out << text;
}

std::map<std::string, std::string> golden(const std::string& name) {
    std::map<std::string, std::string> out;
    std::ifstream in(wt::root_path("tests/data/cwapplink/" + name));
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const size_t space = line.find(' ');
        if (space != std::string::npos) out[line.substr(0, space)] = line.substr(space + 1);
    }
    return out;
}

std::vector<uint8_t> base_rom() {
    return std::vector<uint8_t>(wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize);
}

std::string icon(const char* name) { return wt::root_path(std::string("tests/data/cwmode/") + name); }

// One extra app source.  The icon paths are absolute; the ROM does not contain
// them, so the generated code stays deterministic across machines.
std::string extra_source(int slot, const char* icon_a, const char* icon_b, const char* text) {
    return wiz::fmt(
        "#for 580\n#cwapp %d \"%s\" %s %s\n"
        "void main() { clear(); print(2, 8, \"%s\"); render(); while (1) { delay(2); } }\n",
        slot, text, icon_a, icon_b, text);
}

bool block_matches(const std::vector<uint8_t>& rom, const wiz::CwappLinkApp& app) {
    try {
        wiz::BuildResult built = wiz::build_path(app.path, "cwapp", "", app.base).r;
        if (built.rom.size() < app.base + app.size) return false;
        return std::equal(rom.begin() + app.base, rom.begin() + app.base + app.size,
                          built.rom.begin() + app.base);
    } catch (const std::exception&) {
        return false;
    }
}

void check_keys() {
    const char* labels[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "A", "B", "C",
                            "D", "E", "F", "x", "y", "z", "M", "+", "-", "*", "/", "."};
    bool all = true;
    for (int i = 1; i <= 24; i++)
        if (wiz::slot_key_label(i) != labels[i - 1]) all = false;
    wt::check("cwapplink: slot keys 1..24 are 1..9 A..F x y z M + - * / .", all);
    wt::check("cwapplink: slot 25 has no shortcut key", wiz::slot_key_label(25).empty());
    wt::check("cwapplink: slot 0 and 99 have no shortcut key",
              wiz::slot_key_label(0).empty() && wiz::slot_key_label(99).empty());
    wt::check("cwapplink: hardware legends are ASCII-safe",
              wiz::slot_key_legend(10) == "A (-)" && wiz::slot_key_legend(11) == "B deg" &&
                  wiz::slot_key_legend(12) == "C x^-1" && wiz::slot_key_legend(16) == "x (" &&
                  wiz::slot_key_legend(18) == "z s<>d" && wiz::slot_key_legend(19) == "M M+");
}

void check_two_apps() {
    const std::string manifest = wt::root_path("examples/cwapplink/demo.cwapplink");
    std::error_code ec;
    if (!fs::is_regular_file(manifest, ec)) {
        wt::check("cwapplink 2-app: example project exists", false, manifest);
        return;
    }
    const std::map<std::string, std::string> g = golden("golden_2.txt");
    wiz::CwappLinkResult r = wiz::build_cwapplink(manifest, base_rom(), false);
    wt::check("cwapplink 2-app: build succeeds", r.ok, r.error);
    if (!r.ok) return;
    wt::check("cwapplink 2-app: two apps", r.apps.size() == 2);
    wt::check("cwapplink 2-app: sorted by slot with keys D and E",
              r.apps.size() == 2 && r.apps[0].slot == 13 && r.apps[0].key == "D" &&
                  r.apps[1].slot == 14 && r.apps[1].key == "E");
    wt::check("cwapplink 2-app: mode count 11 -> 13", r.rom[0x3EA0] == 13);
    wt::check("cwapplink 2-app: apps have distinct blocks and entries",
              r.apps.size() == 2 && r.apps[0].base != r.apps[1].base &&
                  r.apps[0].entry != r.apps[1].entry &&
                  r.apps[0].base + r.apps[0].size <= r.apps[1].base);
    wt::check("cwapplink 2-app: blocks match standalone builds at the same base",
              r.apps.size() == 2 && block_matches(r.rom, r.apps[0]) &&
                  block_matches(r.rom, r.apps[1]));
    if (g.count("output_sha256")) {
        wt::check("cwapplink 2-app: golden byte hash",
                  wt::sha256_hex(r.rom) == g.at("output_sha256"), wt::sha256_hex(r.rom));
        wt::check("cwapplink 2-app: golden base ROM", wt::sha256_hex(base_rom()) ==
                                                          g.at("base_sha256"));
    }
}

void check_thirteen_apps(TempDir& dir) {
    std::vector<std::string> names;
    const char* icons[] = {"icon_580_a.png", "icon_580_b.png", "icon_580_c.png",
                           "icon_580_d.png"};
    for (int slot = 13; slot <= 25; slot++) {
        const std::string name = wiz::fmt("app%d.cwiz", slot);
        const std::string text = wiz::fmt("APP %d", slot);
        const std::string a = icon(icons[(slot - 12) % 4]);
        const std::string b = icon(icons[(slot - 11) % 4]);
        write_file(dir.path + "/" + name, extra_source(slot, a.c_str(), b.c_str(), text.c_str()));
        names.push_back(name);
    }
    std::string manifest = "# thirteen apps\n\n";
    for (const std::string& name : names) manifest += name + "\n";
    write_file(dir.path + "/project.cwapplink", manifest);

    const std::map<std::string, std::string> g = golden("golden_13.txt");
    wiz::CwappLinkResult r = wiz::build_cwapplink(dir.path + "/project.cwapplink", base_rom(), false);
    wt::check("cwapplink 13-app: build succeeds", r.ok, r.error);
    if (!r.ok) return;
    wt::check("cwapplink 13-app: thirteen apps", r.apps.size() == 13);
    wt::check("cwapplink 13-app: mode count 11 -> 24", r.rom[0x3EA0] == 24);
    bool keys_ok = r.apps.size() == 13;
    bool blocks_ok = r.apps.size() == 13;
    bool entries_ok = r.apps.size() == 13;
    for (size_t i = 0; i < r.apps.size(); i++) {
        const wiz::CwappLinkApp& app = r.apps[i];
        const int slot = 13 + (int)i;
        if (app.slot != slot) keys_ok = false;
        if (slot <= 24 && app.key != wiz::slot_key_label(slot)) keys_ok = false;
        if (slot == 25 && !app.key.empty()) keys_ok = false;
        if (!block_matches(r.rom, app)) blocks_ok = false;
        if (i && r.apps[i - 1].base + r.apps[i - 1].size > app.base) blocks_ok = false;
        for (size_t j = 0; j < i; j++)
            if (r.apps[j].entry == app.entry) entries_ok = false;
    }
    wt::check("cwapplink 13-app: slots 13..24 carry D..'.' and slot 25 has no key", keys_ok);
    wt::check("cwapplink 13-app: blocks are ordered, distinct and match relocated builds",
              blocks_ok);
    wt::check("cwapplink 13-app: every mode entry is distinct", entries_ok);
    if (g.count("output_sha256")) {
        wt::check("cwapplink 13-app: golden byte hash",
                  wt::sha256_hex(r.rom) == g.at("output_sha256"), wt::sha256_hex(r.rom));
    }
}

void check_preprocessor(TempDir& dir) {
    wiz::CwizPreprocessInfo info = wiz::preprocess_cwiz_info(
        "#for 580\n#cwapp 13 \"Multi App\" a.png b.png\nvoid main() { }\n", "t.cwiz",
        "cwapp");
    wt::check("cwapplink directive: extra form parses slot, text and both icons",
              info.cwapp_extra && info.cwapp_slot == 13 && info.cwapp_text == "Multi App" &&
                  info.cwapp_icon == "a.png" && info.cwapp_icon_selected == "b.png");

    const std::string source =
        "#for 580\n#cwapp 13 \"X\" a.png b.png\n#slot 2\nvoid main() { }\n";
    try {
        wiz::build_cwiz(source, "cwapp", dir.path + "/conflict.cwiz");
        wt::check("cwapplink directive: #slot with the extra #cwapp form is rejected", false);
    } catch (const wiz::CompileError& e) {
        wt::check("cwapplink directive: #slot with the extra #cwapp form is rejected",
                  std::string(e.what()).find("#slot is not used") != std::string::npos, e.what());
    }
}

void check_legacy(TempDir& dir) {
    const std::string source = wt::root_path("examples/cwapp/01_cwapp.cwiz");
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) return;
    write_file(dir.path + "/legacy.cwapplink", source + "\n");
    wiz::CwappLinkResult r = wiz::build_cwapplink(dir.path + "/legacy.cwapplink", base_rom(), false);
    wt::check("cwapplink legacy single: build succeeds", r.ok, r.error);
    if (!r.ok) return;
    wiz::BuildResult built = wiz::build_path(source, "cwapp").r;
    wiz::RomPatchReport legacy =
        wiz::patch_rom_image(base_rom(), built, built.slot, false, true);
    wt::check("cwapplink legacy single: byte-for-byte the existing patch",
              legacy.ok && legacy.rom == r.rom);
    wt::check("cwapplink legacy single: slot key label",
              !r.apps.empty() && r.apps[0].legacy && r.apps[0].key == "1");

    // A Wiz (.wz) legacy source is also a valid single legacy app.
    const std::string wiz_source = wt::root_path("examples/cwapp/02_app_hello.wz");
    if (fs::is_regular_file(wiz_source, ec)) {
        write_file(dir.path + "/wizlegacy.cwapplink", wiz_source + "\n");
        wiz::CwappLinkResult wl =
            wiz::build_cwapplink(dir.path + "/wizlegacy.cwapplink", base_rom(), false);
        wiz::BuildResult wb = wiz::build_path(wiz_source, "cwapp").r;
        wiz::RomPatchReport wp = wiz::patch_rom_image(base_rom(), wb, wb.slot, false, true);
        wt::check("cwapplink legacy .wz: build succeeds and matches the legacy patch",
                  wl.ok && wp.ok && wl.rom == wp.rom, wl.error);
    }

    // Three legacy apps exercise the R8 dispatcher and the 1/9/A shortcut keys.
    for (int slot : {1, 9, 10}) {
        const std::string name = wiz::fmt("legacy%d.cwiz", slot);
        write_file(dir.path + "/" + name,
                   wiz::fmt("#for 580\n#cwapp\n#slot %d\n"
                            "void main() { clear(); print(2, 8, \"L%d\"); render(); "
                            "while (1) { delay(2); } }\n",
                            slot, slot));
    }
    write_file(dir.path + "/legacy3.cwapplink", "legacy1.cwiz\nlegacy9.cwiz\nlegacy10.cwiz\n");
    wiz::CwappLinkResult m =
        wiz::build_cwapplink(dir.path + "/legacy3.cwapplink", base_rom(), false);
    wt::check("cwapplink multi-legacy: build succeeds", m.ok, m.error);
    if (!m.ok) return;
    wt::check("cwapplink multi-legacy: keys 1, 9, A",
              m.apps.size() == 3 && m.apps[0].key == "1" && m.apps[1].key == "9" &&
                  m.apps[2].key == "A");
    wt::check("cwapplink multi-legacy: dispatcher at 3:0E40",
              m.rom[0x30E40] == 0xA0 && m.rom[0x30E41] == 0x78 && m.rom[0x30E44] == 0x00 &&
                  m.rom[0x30E48] == 0xA1 && m.rom[0x30E49] == 0x78);
    wt::check("cwapplink multi-legacy: hook is B 3:0E40",
              m.rom[0x25E1E] == 0x00 && m.rom[0x25E1F] == 0xF3 && m.rom[0x25E20] == 0x40 &&
                  m.rom[0x25E21] == 0x0E);
    bool slots_ok = true;
    for (size_t i = 0; i < m.apps.size(); i++) {
        const int off = 0x7FF8 + 6 * (m.apps[i].slot - 1) + 2;
        if (m.rom[(size_t)off] != m.apps[i].mode_code || m.rom[(size_t)off + 1] != 0)
            slots_ok = false;
    }
    wt::check("cwapplink multi-legacy: menu slots carry the dispatcher ids", slots_ok);

    // Mixed project: one legacy slot plus one appended mode.
    write_file(dir.path + "/mixed.cwapplink", source + "\napp13mix.cwiz\n");
    write_file(dir.path + "/app13mix.cwiz",
               extra_source(13, icon("icon_580_a.png").c_str(), icon("icon_580_b.png").c_str(),
                            "MIXED"));
    wiz::CwappLinkResult mixed =
        wiz::build_cwapplink(dir.path + "/mixed.cwapplink", base_rom(), false);
    wt::check("cwapplink mixed: legacy slot plus appended mode builds", mixed.ok, mixed.error);
    if (mixed.ok) {
        wt::check("cwapplink mixed: keys 1 and D, legacy block first",
                  mixed.apps.size() == 2 && mixed.apps[0].legacy && mixed.apps[0].key == "1" &&
                      mixed.apps[0].base == 0x30E40 && mixed.apps[1].key == "D" &&
                      mixed.apps[1].base >= 0x30E40 + mixed.apps[0].size);
        wt::check("cwapplink mixed: mode count 11 -> 12", mixed.rom[0x3EA0] == 12);
    }
}

void check_errors(TempDir& dir) {
    // Duplicate slot.
    write_file(dir.path + "/dup.cwapplink", "app13.cwiz\napp13b.cwiz\n");
    write_file(dir.path + "/app13.cwiz",
               extra_source(13, icon("icon_580_a.png").c_str(), icon("icon_580_b.png").c_str(),
                            "DUP A"));
    write_file(dir.path + "/app13b.cwiz",
               extra_source(13, icon("icon_580_c.png").c_str(), icon("icon_580_d.png").c_str(),
                            "DUP B"));
    wiz::CwappLinkResult dup =
        wiz::build_cwapplink(dir.path + "/dup.cwapplink", base_rom(), false);
    wt::check("cwapplink error: duplicate slot is rejected",
              !dup.ok && dup.error.find("duplicate app slot 13") != std::string::npos, dup.error);

    // Stock key conflict.
    write_file(dir.path + "/conflict.cwiz",
               extra_source(1, icon("icon_580_a.png").c_str(), icon("icon_580_b.png").c_str(),
                            "CONFLICT"));
    write_file(dir.path + "/conflict.cwapplink", "conflict.cwiz\n");
    wiz::CwappLinkResult conflict =
        wiz::build_cwapplink(dir.path + "/conflict.cwapplink", base_rom(), false);
    wt::check("cwapplink error: extra app on stock key 1 is rejected",
              !conflict.ok && conflict.error.find("stock menu shortcut key 1") != std::string::npos,
              conflict.error);

    // Gap in the keyed slots.
    write_file(dir.path + "/gap15.cwiz",
               extra_source(15, icon("icon_580_a.png").c_str(), icon("icon_580_b.png").c_str(),
                            "GAP 15"));
    write_file(dir.path + "/gap.cwapplink", "app13.cwiz\ngap15.cwiz\n");
    wiz::CwappLinkResult gap = wiz::build_cwapplink(dir.path + "/gap.cwapplink", base_rom(), false);
    wt::check("cwapplink error: keyed slot gap is rejected",
              !gap.ok && gap.error.find("needs slot 14") != std::string::npos, gap.error);

    // Non-cwapp source.
    write_file(dir.path + "/plain.cwiz", "#for 580\nvoid main() { }\n");
    write_file(dir.path + "/plain.cwapplink", "plain.cwiz\n");
    wiz::CwappLinkResult plain =
        wiz::build_cwapplink(dir.path + "/plain.cwapplink", base_rom(), false);
    wt::check("cwapplink error: non-cwapp source is rejected",
              !plain.ok && plain.error.find("not a #cwapp source") != std::string::npos,
              plain.error);

    // Missing source.
    write_file(dir.path + "/missing.cwapplink", "does-not-exist.cwiz\n");
    wiz::CwappLinkResult missing =
        wiz::build_cwapplink(dir.path + "/missing.cwapplink", base_rom(), false);
    wt::check("cwapplink error: missing source is rejected",
              !missing.ok && missing.error.find("not found") != std::string::npos, missing.error);

    // 880 base ROM.
    write_file(dir.path + "/app13only.cwapplink", "app13.cwiz\n");
    std::vector<uint8_t> big(0x60000, 0xFF);
    wiz::CwappLinkResult wrong =
        wiz::build_cwapplink(dir.path + "/app13only.cwapplink", big, false);
    wt::check("cwapplink error: non-580 base ROM is rejected",
              !wrong.ok && wrong.error.find("262144") != std::string::npos, wrong.error);

    // BOM-prefixed manifest.
    write_file(dir.path + "/bom.cwapplink", "\xEF\xBB\xBF" "app13.cwiz\n");
    wiz::CwappLinkResult bom =
        wiz::build_cwapplink(dir.path + "/bom.cwapplink", base_rom(), false);
    wt::check("cwapplink: UTF-8 BOM in the manifest is accepted", bom.ok, bom.error);

    // Wiz sources cannot use the extra form.
    try {
        wiz::build_source("#for cwapp\n#cwapp 13 \"X\" a.png b.png\ndef main():\n    pass\n",
                          "cwapp", dir.path + "/extra.wz");
        wt::check("cwapplink error: the extra form is CWiz only", false);
    } catch (const wiz::CompileError& e) {
        wt::check("cwapplink error: the extra form is CWiz only",
                  std::string(e.what()).find("CWiz only") != std::string::npos, e.what());
    }

    // A non-blank code block needs --force.
    {
        std::vector<uint8_t> dirty = base_rom();
        dirty[0x30E40] = 0x00;
        wiz::CwappLinkResult no = wiz::build_cwapplink(
            dir.path + "/app13only.cwapplink", dirty, false);
        wt::check("cwapplink error: non-blank code block is rejected",
                  !no.ok && no.error.find("not free flash") != std::string::npos, no.error);
        wiz::CwappLinkResult forced = wiz::build_cwapplink(
            dir.path + "/app13only.cwapplink", dirty, true);
        wt::check("cwapplink: --force accepts a non-blank code block", forced.ok, forced.error);
    }

    // A project that does not fit fails before writing.
    {
        std::string manifest;
        for (int slot = 13; slot <= 36; slot++) {
            const std::string name = wiz::fmt("big%d.cwiz", slot);
            write_file(dir.path + "/" + name,
                       extra_source(slot, icon("icon_580_a.png").c_str(),
                                    icon("icon_580_b.png").c_str(),
                                    wiz::fmt("BIG %d", slot).c_str()));
            manifest += name + "\n";
        }
        write_file(dir.path + "/big.cwapplink", manifest);
        wiz::CwappLinkResult big =
            wiz::build_cwapplink(dir.path + "/big.cwapplink", base_rom(), false);
        wt::check("cwapplink error: a project that does not fit is rejected",
                  !big.ok && big.error.find("does not fit") != std::string::npos, big.error);
    }

    // Nothing was written on any failure.
    wt::check("cwapplink error: failures leave no ROM", dup.rom.empty() && conflict.rom.empty() &&
                                                           gap.rom.empty() && plain.rom.empty() &&
                                                           missing.rom.empty() &&
                                                           wrong.rom.empty());
}

void check_cli(TempDir& dir) {
    const std::string manifest = wt::root_path("examples/cwapplink/demo.cwapplink");
    std::error_code ec;
    if (!fs::is_regular_file(manifest, ec)) return;
    wt::CliResult good = wt::cli_run(dir.path, {manifest, "-o", "cli.bin"});
    wt::check("cwapplink CLI: wizc builds the project", good.rc == 0, good.out);
    const std::vector<uint8_t> rom = wiz::read_binary_file(dir.path + "/cli.bin", nullptr);
    wt::check("cwapplink CLI: output is a 256 KB ROM", rom.size() == 262144);
    wt::check("cwapplink CLI: output lists the shortcut keys",
              good.out.find("key D") != std::string::npos &&
                  good.out.find("key E") != std::string::npos);

    write_file(dir.path + "/bad.cwapplink", "app13.cwiz\napp13b.cwiz\n");
    wt::CliResult bad = wt::cli_run(dir.path, {dir.path + "/bad.cwapplink", "-o", "bad.bin"});
    wt::check("cwapplink CLI: duplicate slot fails", bad.rc != 0);
    wt::check("cwapplink CLI: a failed build writes no output",
              !fs::exists(dir.path + "/bad.bin", ec));

    const std::string extra = wt::root_path("examples/cwapplink/app_a.cwiz");
    wt::CliResult one = wt::cli_run(dir.path, {extra, "-o", "one.bin"});
    wt::check("cwapplink CLI: a single extra #cwapp source builds a ROM",
              one.rc == 0 && fs::file_size(dir.path + "/one.bin", ec) == 262144, one.out);
    wt::check("cwapplink CLI: the single app reports key D",
              one.out.find("key D") != std::string::npos);

    wt::CliResult bad_target = wt::cli_run(
        dir.path, {manifest, "-o", "880.bin", "--target", "880"});
    wt::check("cwapplink CLI: --target 880 is rejected for a link project",
              bad_target.rc != 0 && !fs::exists(dir.path + "/880.bin", ec));
}

}  // namespace

namespace wt {

void run_cwlink() {
    check_keys();
    check_two_apps();
    TempDir dir("run");
    check_thirteen_apps(dir);
    check_preprocessor(dir);
    check_legacy(dir);
    check_errors(dir);
    check_cli(dir);
}

}  // namespace wt
