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
#include "cwicon.hpp"
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

void check_two_apps(TempDir& dir) {
    // Use a generated project so the golden hash does not depend on editable
    // example files; the repository demo is still built below.
    const std::string manifest = dir.path + "/project.cwapplink";
    write_file(dir.path + "/a.cwiz", extra_source(13, icon("icon_580_a.png").c_str(),
                                                  icon("icon_580_b.png").c_str(), "APP A"));
    write_file(dir.path + "/b.cwiz", extra_source(14, icon("icon_580_c.png").c_str(),
                                                  icon("icon_580_d.png").c_str(), "APP B"));
    write_file(manifest, "a.cwiz\nb.cwiz\n");
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

void check_three_arg_icons(TempDir& dir) {
    const std::string icon_path = icon("icon_580_a.png");
    const std::string inv_path = dir.path + "/inverse.png";
    {
        wiz::cwmode::Image image, inverse;
        std::string error;
        if (!wiz::cwmode::load_image(icon_path, image, error)) {
            wt::check("three-arg: load fixture", false, error);
            return;
        }
        inverse = wiz::cwmode::invert_image(image);
        if (!wiz::write_png(inv_path, inverse, error)) {
            wt::check("three-arg: write inverse PNG", false, error);
            return;
        }
    }
    auto tiny = [](int slot, const std::string& text) {
        return wiz::fmt("#for 580\n#cwapp %d \"%s\" ../../../tests/data/cwmode/icon_580_a.png\n"
                        "void main() { delay(1); }\n",
                        slot, text.c_str());
    };
    write_file(dir.path + "/three.cwiz", tiny(13, "THREE"));
    write_file(dir.path + "/four.cwiz",
               wiz::fmt("#for 580\n#cwapp 13 \"THREE\" %s %s\n"
                        "void main() { delay(1); }\n",
                        icon_path.c_str(), inv_path.c_str()));
    for (int n : {1, 2, 6, 30}) {
        std::string manifest;
        for (int i = 0; i < n; i++) {
            const int slot = 13 + i;
            const std::string name = wiz::fmt("n%d_%d.cwiz", n, slot);
            write_file(dir.path + "/" + name, tiny(slot, wiz::fmt("A%d", slot).c_str()));
            manifest += name + "\n";
        }
        write_file(dir.path + "/many.cwapplink", manifest);
        wiz::CwappLinkResult r = wiz::build_cwapplink(
            dir.path + "/many.cwapplink", {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize}, false);
        wt::check(wiz::fmt("three-arg: %d apps build", n), r.ok, r.error);
        if (r.ok)
            wt::check(wiz::fmt("three-arg: %d apps registered", n), (int)r.apps.size() == n);
    }

    // The builder decorates the original PNG; passing an already decorated
    // cwicon output must not paint or crop it a second time.
    {
        wiz::cwmode::Image raw_image;
        std::string error;
        if (wiz::cwmode::load_image(icon_path, raw_image, error)) {
            const wiz::cwmode::Image decorated =
                wiz::cwmode::prepare_mode_icon(raw_image, wiz::cwmode::Model::Cw580);
            const std::string decorated_path = dir.path + "/decorated.png";
            if (wiz::write_png(decorated_path, decorated, error)) {
                write_file(dir.path + "/raw3.cwiz", tiny(13, "DECOR"));
                write_file(dir.path + "/dec3.cwiz",
                           wiz::fmt("#for 580\n#cwapp 13 \"DECOR\" %s\n"
                                    "void main() { delay(1); }\n",
                                    decorated_path.c_str()));
                write_file(dir.path + "/raw3.cwapplink", "raw3.cwiz\n");
                write_file(dir.path + "/dec3.cwapplink", "dec3.cwiz\n");
                wiz::CwappLinkResult a = wiz::build_cwapplink(
                    dir.path + "/raw3.cwapplink",
                    {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize}, false);
                wiz::CwappLinkResult b = wiz::build_cwapplink(
                    dir.path + "/dec3.cwapplink",
                    {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize}, false);
                wt::check("three-arg: builder decoration is idempotent", a.ok && b.ok &&
                                                                          a.rom == b.rom,
                          a.ok && b.ok ? "" : a.error + b.error);
            }
        }
    }

    // A three-argument source and a four-argument source with the inverse PNG
    // must produce byte-identical ROMs.
    write_file(dir.path + "/three.cwapplink", "three.cwiz\n");
    write_file(dir.path + "/four.cwapplink", "four.cwiz\n");
    wiz::CwappLinkResult a = wiz::build_cwapplink(
        dir.path + "/three.cwapplink", {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize}, false);
    wiz::CwappLinkResult b = wiz::build_cwapplink(
        dir.path + "/four.cwapplink", {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize}, false);
    wt::check("three-arg: implied inverse matches the explicit inverse image",
              a.ok && b.ok && a.rom == b.rom,
              a.ok && b.ok ? wiz::fmt("%s vs %s", wt::sha256_hex(a.rom).c_str(),
                                       wt::sha256_hex(b.rom).c_str())
                           : a.error + b.error);
}

void check_no_image(TempDir& dir) {
    // Parser: only slot + quoted text is the no-image extra form.
    auto parse_fails = [](const std::string& src, const std::string& needle) {
        try {
            wiz::build_cwiz(src, "cwapp");
        } catch (const std::exception& e) {
            return std::string(e.what()).find(needle) != std::string::npos;
        }
        return false;
    };
    wt::check("no-image: one argument is rejected",
              parse_fails("#for 580\n#cwapp 13\nvoid main() {}\n", "needs a slot and a text"));
    wt::check("no-image: unquoted text is rejected",
              parse_fails("#for 580\n#cwapp 13 BLANK\nvoid main() {}\n", "must be quoted"));
    wt::check("no-image: empty text is rejected",
              parse_fails("#for 580\n#cwapp 13 \"\"\nvoid main() {}\n", "text is empty"));

    // A no-image app is the decorated blank tile; a fully white image goes
    // through exactly the same prepare step and must produce the same ROM.
    {
        wiz::cwmode::Image white;
        white.width = 48;
        white.height = 23;
        white.rgba.assign(48 * 23 * 4, 255);
        std::string error;
        const std::string white_path = dir.path + "/white.png";
        if (!wiz::write_png(white_path, white, error)) {
            wt::check("no-image: write white fixture", false, error);
            return;
        }
        write_file(dir.path + "/blank.cwiz",
                   "#for 580\n#cwapp 13 \"BLANK\"\nvoid main() { delay(1); }\n");
        write_file(dir.path + "/white.cwiz",
                   wiz::fmt("#for 580\n#cwapp 13 \"BLANK\" %s\n"
                            "void main() { delay(1); }\n",
                            white_path.c_str()));
        write_file(dir.path + "/blank.cwapplink", "blank.cwiz\n");
        write_file(dir.path + "/white.cwapplink", "white.cwiz\n");
        wiz::CwappLinkResult blank = wiz::build_cwapplink(
            dir.path + "/blank.cwapplink", {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize},
            false);
        wiz::CwappLinkResult white_rom = wiz::build_cwapplink(
            dir.path + "/white.cwapplink", {wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize},
            false);
        wt::check("no-image: blank app builds", blank.ok, blank.error);
        wt::check("no-image: no icon path is reported", blank.ok && blank.apps.size() == 1 &&
                                                           blank.apps[0].icon.empty());
        wt::check("no-image: decorated blank equals a white 48x23 source",
                  blank.ok && white_rom.ok && blank.rom == white_rom.rom,
                  blank.ok && white_rom.ok ? "" : blank.error + white_rom.error);
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
    wt::check("cwapplink multi-legacy: dispatcher clears the boot flag then compares ids",
              m.rom[0x30E40] == 0x00 && m.rom[0x30E41] == 0x00 && m.rom[0x30E42] == 0x11 &&
                  m.rom[0x30E43] == 0x90 && m.rom[0x30E44] == 0xF4 && m.rom[0x30E45] == 0xE7 &&
                  m.rom[0x30E46] == 0xA0 && m.rom[0x30E47] == 0x78 &&
                  m.rom[0x30E4E] == 0xA1 && m.rom[0x30E4F] == 0x78);
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

    // Boot dispatcher at 3:00302: the power-on path relaunches the app whose
    // mode id is still current, and falls through to the stock menu otherwise.
    {
        const uint32_t boot_addr = 0x30E40 + 8 * 3 + 12 + 6;
        wt::check("cwapplink multi-legacy: boot dispatcher matches ids then sets the flag",
                  m.rom[boot_addr] == 0xA0 && m.rom[boot_addr + 1] == 0x70 &&
                      m.rom[boot_addr + 2] == 0x07 && m.rom[boot_addr + 3] == 0xC8 &&
                      m.rom[boot_addr + 4] == 0x01 && m.rom[boot_addr + 5] == 0x00 &&
                      m.rom[boot_addr + 6] == 0x11 && m.rom[boot_addr + 7] == 0x90 &&
                      m.rom[boot_addr + 8] == 0xF4 && m.rom[boot_addr + 9] == 0xE7);
        wt::check("cwapplink multi-legacy: main hook calls the boot dispatcher",
                  m.rom[0x29448] == 0x01 && m.rom[0x29449] == 0xF3 &&
                      m.rom[0x2944A] == (uint8_t)(boot_addr & 0xFF) &&
                      m.rom[0x2944B] == (uint8_t)((boot_addr >> 8) & 0xFF));
        const wiz::CwappLinkApp& second = m.apps[1];
        wiz::U8 b(m.rom, {}, "cwapp", boot_addr >> 16, boot_addr & 0xFFFF,
                  {{(int)(second.entry >> 16), (int)(second.entry & 0xFFFF)}});
        b.r[0] = second.mode_code;
        std::string why = b.run(200000);
        wt::check("cwapplink multi-legacy: boot dispatch relaunches the second app",
                  why == wiz::fmt("stop at %X:%04X", second.entry >> 16, second.entry & 0xFFFF),
                  why);
        wiz::U8 n(m.rom, {}, "cwapp", boot_addr >> 16, boot_addr & 0xFFFF, {{2, 0xE234}});
        n.r[0] = 0x0D;
        why = n.run(200000);
        wt::check("cwapplink multi-legacy: boot dispatch keeps mode 0D on the menu path",
                  why == "stop at 2:E234", why);
    }

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
        wt::check("cwapplink mixed: main hook goes to the legacy boot dispatcher",
                  mixed.rom[0x29448] == 0x00 && mixed.rom[0x29449] == 0xF3);
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
    write_file(dir.path + "/cli_a.cwiz",
               extra_source(13, icon("icon_580_a.png").c_str(), icon("icon_580_b.png").c_str(),
                            "CLI A"));
    write_file(dir.path + "/cli_b.cwiz",
               extra_source(14, icon("icon_580_c.png").c_str(), icon("icon_580_d.png").c_str(),
                            "CLI B"));
    write_file(dir.path + "/cli.cwapplink", "cli_a.cwiz\ncli_b.cwiz\n");
    const std::string manifest = dir.path + "/cli.cwapplink";
    std::error_code ec;
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

    const std::string extra = dir.path + "/cli_a.cwiz";
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
    TempDir dir("run");
    check_two_apps(dir);
    check_thirteen_apps(dir);
    check_preprocessor(dir);
    check_legacy(dir);
    check_three_arg_icons(dir);
    check_no_image(dir);
    check_errors(dir);
    check_cli(dir);
}

}  // namespace wt
