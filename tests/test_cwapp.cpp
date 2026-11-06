// test_cwapp.cpp - cwapp target: a Wiz program living in segment 3 (patched into a
// firmware) (ported from tests/test_cwapp.py).
#include <cstdio>
#include <filesystem>
#include <string>

#include "builtin_rom.hpp"
#include "harness.hpp"
#include "patch.hpp"
#include "sha256.hpp"

using namespace wt;

namespace {

std::string bin_sibling(const std::string& name) {
    return (std::filesystem::path(g_wizc()).parent_path() / name).string();
}

Machine run_built(wiz::BuildResult br, const std::string& tg, long steps, bool press = false,
                  long press_steps = 0, const std::string& key = "") {
    int csr = -1, pc = -1;
    std::vector<std::pair<int, int>> stop;
    if (tg == "cwapp") {
        // Enter through _TRAMP with the app's mode id, like the firmware's
        // set_mode hook does (this also clears the boot-path flag).
        int tramp = br.syms.at("_TRAMP");
        csr = tramp >> 16;
        pc = tramp & 0xFFFF;
        // MENU/exit() draw the firmware mode menu through 2:AD4C; the reset
        // handler is only the fallback when the saved stack did not fit.
        stop = {{2, 0xAD4C}, {2, 0x5E22}, {0, 0xA1CE}};
    }
    wiz::U8 m(br.rom, {}, tg, csr, pc, stop);
    if (csr >= 0) m.r[8] = 0xE0;
    if (press) m.run(press_steps);
    if (press) {
        int k = wiz::key_consts(tg).at(key);
        m.keys.push_back(key_pair(k));
    }
    std::string why = m.run(steps);
    return Machine{std::move(m), br.variables, why, br.rom, br.full_asm, br.syms};
}

Machine run_app(const std::string& src, const std::string& tg, long steps, bool press = false,
                long press_steps = 0, const std::string& key = "") {
    return run_built(wiz::build_source(src, tg), tg, steps, press, press_steps, key);
}

Machine run_cwiz_app(const std::string& src, long steps, bool press = false, long press_steps = 0,
                     const std::string& key = "") {
    return run_built(wiz::build_cwiz(src, "cwapp"), "cwapp", steps, press, press_steps, key);
}

int run_prog(const std::string& prog, const std::vector<std::string>& args, std::string* out) {
    std::string cmd = "'" + prog + "'";
    for (auto& a : args) cmd += " '" + a + "'";
    cmd += " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    char buf[4096];
    while (std::fgets(buf, sizeof(buf), p)) {
        if (out) *out += buf;
    }
    int st = pclose(p);
    return WIFEXITED(st) ? WEXITSTATUS(st) : st;
}

}  // namespace

void wt::run_cwapp() {
    std::string SRC = wiz::read_text_file(root_path("examples/cwapp/02_app_hello.wz"));
    wiz::BuildResult br = wiz::build_source(SRC, "cwapp");
    check("cwapp: code is placed at 3:0E40 (trampoline first)",
          br.syms.at("_TRAMP") == 0x30E40 && br.syms.at("_APP") > 0x30E40);
    bool blank = true;
    size_t lim = std::min<size_t>(0x30E40, br.rom.size());
    for (size_t i = 0; i < lim; i++)
        if (br.rom[i] != 0xFF) blank = false;
    check("cwapp: nothing is emitted before 3:0E40 (image below it is blank flash, 0xFF)", blank);

    Machine a = run_app(SRC, "cwapp", 3000000);
    std::string cw_src = SRC;
    cw_src = wiz::replace_all(cw_src, "for cwapp", "for cw");
    cw_src = wiz::replace_all(cw_src, "exit()", "pass");
    Machine b = run_app(cw_src, "cw", 3000000);
    int hashes = 0;
    for (char ch : a.m.ascii())
        if (ch == '#') hashes++;
    check("cwapp: screen is pixel-identical to the same program built for cw (segment 0)",
          a.m.ascii() == b.m.ascii() && hashes > 50, std::to_string(hashes));
    Machine c = run_app(SRC, "cwapp", 6000000, true, 1500000, "K_AC");
    check("cwapp: AC reaches key_down and the app's exit() opens the firmware menu",
          c.why == "stop at 2:AD4C", c.why);

    // Key polling app: AC must come back as scan index 29, MENU must leave
    // through the saved set_mode continuation without a reset, and exit() /
    // returning from main() must use the same clean path.
    const std::string key_src =
        "int R = -2;\n"
        "void main() { while (1) { int k = key(); if (k >= 0) { R = k; delay(1); } } }\n";
    Machine ac = run_cwiz_app(key_src, 2000000, true, 300000, "K_AC");
    check("cwapp: key() returns K_AC (29) while the app keeps running",
          uvar_of(ac.m, ac.vars, "R") == 29 && ac.why == "max steps",
          wiz::fmt("%d %s", uvar_of(ac.m, ac.vars, "R"), ac.why.c_str()));
    Machine menu = run_cwiz_app(key_src, 3000000, true, 1500000, "K_MENU");
    check("cwapp: MENU opens the firmware menu without resetting",
          menu.why == "stop at 2:AD4C", menu.why);

    const std::string exit_src = "void main() { clear(); render(); exit(); }\n";
    Machine ex = run_cwiz_app(exit_src, 3000000);
    check("cwapp: exit() opens the firmware menu without resetting", ex.why == "stop at 2:AD4C",
          ex.why);
    const std::string ret_src = "void main() { clear(); render(); }\n";
    Machine rt = run_cwiz_app(ret_src, 3000000);
    check("cwapp: returning from main() opens the firmware menu without resetting",
          rt.why == "stop at 2:AD4C", rt.why);

    // Shift+AC (OFF) follows the stock power-off path: LCD off (F03D), FCON
    // clear (F00A), every interrupt masked (F010..F017), then STOPACP/SBYCON.
    {
        const std::string off_src = "void main() { while (1) { key(); delay(1); } }\n";
        wiz::BuildResult ob = wiz::build_cwiz(off_src, "cwapp");
        int tramp = ob.syms.at("_TRAMP");
        const auto& kc = wiz::key_consts("cwapp");
        wiz::U8 m(ob.rom,
                  {key_pair(kc.at("K_SHIFT")), key_pair(kc.at("K_AC"))},
                  "cwapp", tramp >> 16, tramp & 0xFFFF, {});
        m.r[8] = 0xE0;
        std::string why = m.run(2000000);
        bool masked = true;
        for (int i = 0; i < 8; i++)
            if (m.mem[0xF010 + i] != 0) masked = false;
        check("cwapp: Shift+AC powers down (F03D/F00A cleared, F008=0xA0, F009=0x02)",
              m.mem[0xF03D] == 0 && m.mem[0xF00A] == 0 && m.mem[0xF008] == 0xA0 &&
                  m.mem[0xF009] == 0x02 && masked,
              wiz::fmt("F03D=%02X F00A=%02X F008=%02X F009=%02X %s", m.mem[0xF03D],
                       m.mem[0xF00A], m.mem[0xF008], m.mem[0xF009], why.c_str()));
    }

    // wizc embeds the 580 ROM and patches #cwapp programs without --cwapp.
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::path(wt::root_path("temp/cwapp"));
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string src =
            "#for 580\n"
            "void reveal() { }\n"
            "#cwapp\n"
            "#slot 9\n"
            "void reveal() { exit(); }\n"
            "----\n"
            "void main() { clear(); while (1) { } }\n";
        check("test setup: writes a #slot source", wiz::write_text_file((dir / "slot.cwiz").string(), src), "");
        CliResult r = cli_run(dir.string(), {"slot.cwiz", "-o", "slot.bin", "--no-asm"});
        check("wizc: #cwapp is patched automatically (no --cwapp)", r.rc == 0, r.out);
        bool ok = false;
        std::vector<uint8_t> bin = wiz::read_binary_file((dir / "slot.bin").string(), &ok);
        check("wizc: output is a complete 256 KB ROM", ok && bin.size() == 262144,
              std::to_string(bin.size()));
        if (bin.size() == 262144) {
            check("wizc: installs the seg-3 hook", bin[0x25E1E] == 0x00 && bin[0x25E1F] == 0xF3 &&
                                                    bin[0x25E20] == 0x40 && bin[0x25E21] == 0x0E,
                  wiz::fmt("%02x %02x %02x %02x", bin[0x25E1E], bin[0x25E1F], bin[0x25E20],
                           bin[0x25E21]));
            const size_t slot9 = 0x7FF8 + 6 * 8 + 2;
            check("wizc: selects the #slot menu entry", bin[slot9] == 0xE0 && bin[slot9 + 1] == 0x00,
                  wiz::fmt("%02x %02x", bin[slot9], bin[slot9 + 1]));
        }
        std::string runlog;
        int rc = run_prog(bin_sibling("wizrun"),
                          {(dir / "slot.bin").string(), "580", "--steps", "5000"}, &runlog);
        check("wizrun: runs the patched .bin with a model number", rc == 0 && runlog.find("ran") != std::string::npos,
              runlog);
    }

    // Regression: the default internal target is cwii, not the user-facing
    // string "880"; wizrun must still pick the 880 memory map.
    {
        std::string runlog;
        int rc = run_prog(bin_sibling("wizrun"),
                          {root_path("examples/hello/01_hello.cwiz"), "--steps", "300000"},
                          &runlog);
        check("wizrun: a default 880 source runs on the cwii memory map",
              rc == 0 && runlog.find("write to ROM") == std::string::npos, runlog);
        runlog.clear();
        rc = run_prog(bin_sibling("wizrun"),
                      {root_path("examples/hello/01_hello.cwiz"), "--keys", "0xF801"},
                      &runlog);
        check("wizrun: an out-of-range key code is rejected", rc != 0 &&
                  runlog.find("out of range") != std::string::npos,
              runlog);
    }

    // ROM-level checks on the patched image.  The baseline is the bundled
    // firmware dump embedded in wizcore, written under the repo temp area.
    std::string fixture = root_path("temp/cwapp/baseline.bin");
    std::filesystem::create_directories(std::filesystem::path(fixture).parent_path());
    std::vector<uint8_t> baseline(wiz::kFx580VnRom,
                                  wiz::kFx580VnRom + wiz::kFx580VnRomSize);
    // The bundled dump is the known-variant image used by wizc's patcher; make
    // the menu hook bytes match the expected sequence so patch_menu accepts it.
    if (baseline.size() > 0x25E21) {
        baseline[0x25E1E] = 0x03;
        baseline[0x25E1F] = 0xE8;
        baseline[0x25E20] = 0x30;
        baseline[0x25E21] = 0x68;
    }
    wiz::write_binary_file(fixture, baseline);
    std::vector<std::string> candidates = {fixture};
    for (const auto& rp : candidates) {
        std::string out = wt::root_path("temp/cwapp/_cwapp_patched.bin");
        std::string log;
        int rc = run_prog(bin_sibling("patch_menu"),
                          {rp, root_path("examples/cwapp/02_app_hello.wz"), out}, &log);
        check("patch_menu.py runs on the sample ROM", rc == 0, log);
        bool ok1 = false, ok2 = false;
        std::vector<uint8_t> pr = wiz::read_binary_file(out, &ok1);
        std::vector<uint8_t> orig = wiz::read_binary_file(rp, &ok2);
        if (ok1 && ok2) {
            bool only = true;
            for (size_t i = 0; i < orig.size(); i++)
                if (i < pr.size() && orig[i] != pr[i]) {
                    bool inrange = (i >= 0x25E1E && i < 0x25E22) ||
                                   (i >= 0x29448 && i < 0x2944C) || i == 0x8000 || i >= 0x30E40;
                    if (!inrange) only = false;
                }
            check("patch changes only the hook, the boot dispatcher, one menu byte and the "
                  "program area",
                  only);

            const uint32_t bd = (uint32_t)br.syms.at("_BOOT_DISPATCH");
            check("patch: the power-on dispatcher branches to _BOOT_DISPATCH",
                  pr.size() > 0x2944B && pr[0x29448] == 0x01 &&
                      pr[0x29449] == (uint8_t)(0xF0 | ((bd >> 16) & 0xF)) &&
                      pr[0x2944A] == (uint8_t)(bd & 0xFF) &&
                      pr[0x2944B] == (uint8_t)((bd >> 8) & 0xFF),
                  wiz::fmt("%02X %02X %02X %02X -> %05X", pr[0x29448], pr[0x29449],
                           pr[0x2944A], pr[0x2944B], bd));

            // A reboot reaches the mode dispatcher with the running mode id.  E0
            // must re-enter the app; every other id must fall through to the
            // stock menu function so MENU/0x0D still paints the menu.
            {
                wiz::U8 m(pr, {}, "cwapp", bd >> 16, bd & 0xFFFF, {{2, 0xE234}});
                m.r[0] = 0x0D;
                std::string why = m.run(100000);
                check("boot dispatcher: mode 0D falls through to the stock menu at 2:E234",
                      why == "stop at 2:E234", why);
            }
            {
                const uint32_t app_addr = (uint32_t)br.syms.at("_APP");
                wiz::U8 m(pr, {}, "cwapp", bd >> 16, bd & 0xFFFF,
                          {{(int)(app_addr >> 16), (int)(app_addr & 0xFFFF)}});
                m.r[0] = 0xE0;
                std::string why = m.run(100000);
                check("boot dispatcher: mode E0 re-enters the app on the boot path",
                      why == wiz::fmt("stop at %X:%04X", app_addr >> 16, app_addr & 0xFFFF) &&
                          m.mem[0xE7F4] == 1,
                      why);
            }
            struct MC {
                int mode;
                int csr, pc;
            };
            MC modes[] = {{0x03, 2, 0x5E88}, {0x06, 2, 0x5E22}, {0x45, 2, 0x5E22}};
            for (auto& mm : modes) {
                wiz::U8 m(pr, {}, "cwapp", 3, 0x0E40, {{2, 0x5E88}, {2, 0x5E22}});
                m.r[8] = mm.mode;
                m.run(100000);
                check(wiz::fmt("trampoline: mode %02X continues at %X:%04X", mm.mode, mm.csr, mm.pc),
                      m.csr == mm.csr && m.pc == mm.pc);
            }
        }
        break;
    }

    // Non-cwapp images must stay byte-identical.  These SHA-256 values were
    // recorded from the pre-clean-exit compiler and match the current build
    // byte for byte (the cwapp-only runtime lines are filtered out).
    {
        std::string hello = wiz::read_text_file(root_path("examples/hello/01_hello.cwiz"));
        const std::string hcw = wt::sha256_hex(wiz::build_cwiz(hello, "cw").rom);
        const std::string hcwii = wt::sha256_hex(wiz::build_cwiz(hello, "cwii").rom);
        check("non-cwapp: cw runtime image is unchanged",
              hcw == "82491e911da8a85272785c77f1b7f1f102d8cc8f3c9179b0c9eb8f55f9977c3e", hcw);
        check("non-cwapp: cwii runtime image is unchanged",
              hcwii == "42843ea6fe81a478cc95452b4ca1b16fcd25416cbe567270a033b06858496bb8", hcwii);
    }
}
