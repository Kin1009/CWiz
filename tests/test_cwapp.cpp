// test_cwapp.cpp - cwapp target: a Wiz program living in segment 3 (patched into a
// firmware) (ported from tests/test_cwapp.py).
#include <cstdio>
#include <filesystem>
#include <string>

#include "harness.hpp"

using namespace wt;

namespace {

std::string bin_sibling(const std::string& name) {
    return (std::filesystem::path(g_wizc()).parent_path() / name).string();
}

Machine run_app(const std::string& src, const std::string& tg, long steps, bool press = false,
                long press_steps = 0, const std::string& key = "") {
    wiz::BuildResult br = wiz::build_source(src, tg);
    int csr = -1, pc = -1;
    std::vector<std::pair<int, int>> stop;
    if (tg == "cwapp") {
        int app = br.syms.at("_APP");
        csr = app >> 16;
        pc = app & 0xFFFF;
        stop = {{0, 0xA1CE}};
    }
    wiz::U8 m(br.rom, {}, tg, csr, pc, stop);
    if (press) m.run(press_steps);
    if (press) {
        int k = wiz::key_consts(tg).at(key);
        m.keys.push_back(key_pair(k));
    }
    std::string why = m.run(steps);
    return Machine{std::move(m), br.variables, why, br.rom, br.full_asm, br.syms};
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
    check("cwapp: AC -> exit() lands on the firmware reset handler 0:A1CE with SP = F000",
          c.why == "stop at 0:A1CE" && c.m.sp == 0xF000, c.why);

    // wizc embeds the 580 ROM and patches #cwapp programs without --cwapp.
    {
        namespace fs = std::filesystem;
        fs::path dir = fs::temp_directory_path() / "wiz_cwapp_auto";
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

    // ROM-level checks on the patched image (needs the sample ROM next to the repo; skipped otherwise)
    std::vector<std::string> candidates = {"/mnt/user-data/uploads/bro.bin", root_path("bro.bin")};
    for (const auto& rp : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(rp, ec)) continue;
        std::string out = "/tmp/_cwapp_patched.bin";
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
                    bool inrange = (i >= 0x25E1E && i < 0x25E22) || i == 0x8000 || i >= 0x30E40;
                    if (!inrange) only = false;
                }
            check("patch changes only the hook, one menu byte and the program area", only);
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
}
