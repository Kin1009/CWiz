// test_cw.cpp - Wiz 0.4 tests: `for cwii` / `for cw` sections, ML620906 target,
// boot translation, 1-plane runtime (ported from tests/test_cw.py).
#include <filesystem>
#include <set>
#include <string>
#include <unistd.h>

#include "harness.hpp"

using namespace wt;

namespace {

Machine run(const std::string& src, const std::string& target, long steps = 30000000) {
    return run_src(src, target, {}, steps);
}

std::set<std::pair<int, int>> black(const wiz::U8& m) {
    std::set<std::pair<int, int>> out;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 192; x++)
            if (m.pixel(x, y) == 3) out.insert({x, y});
    return out;
}

std::vector<int> enc1(const std::string& line) {
    wiz::Assembler a("nX-U8/100");
    std::map<uint32_t, uint8_t> img = a.assemble("ORG 0\n" + line + "\n");
    std::vector<int> words;
    for (int pc = 0;; pc += 2) {
        bool has = img.count((uint32_t)pc) || img.count((uint32_t)pc + 1);
        if (!has) break;
        int lo = img.count((uint32_t)pc) ? img.at((uint32_t)pc) : 0;
        int hi = img.count((uint32_t)pc + 1) ? img.at((uint32_t)pc + 1) : 0;
        words.push_back(lo | (hi << 8));
    }
    return words;
}

std::vector<std::string> code_lines(const std::string& full) {
    std::vector<std::string> out;
    for (const auto& l : wiz::split(full, '\n')) out.push_back(wiz::split(l, ';')[0]);
    return out;
}

std::set<std::pair<int, int>> top63(const std::set<std::pair<int, int>>& img) {
    std::set<std::pair<int, int>> out;
    for (auto& p : img)
        if (p.second < 63) out.insert(p);
    return out;
}

std::vector<int> vr(const wiz::U8& m, int row) {
    std::vector<int> out;
    for (int i = 0; i < 32; i++) out.push_back(m.planes[0][row * 32 + i]);
    return out;
}

}  // namespace

void wt::run_cw() {
    // ------------------------------------------------- LEA encodings
    {
        std::vector<std::vector<int>> got = {enc1("LEA 0F010H"), enc1("LEA [ER2]"), enc1("LEA 4[ER2]")};
        std::vector<std::vector<int>> want = {{0xF00C, 0xF010}, {0xF02A}, {0xF02B, 4}};
        check("LEA encodings", got == want, ss(got));
    }

    // ------------------------------------------------- section parsing
    std::string dash;
    for (int i = 0; i < 12; i++) dash += "\xE2\x80\x94";
    const std::string SRC = "x = 1\nfor cwii            # ML620909\na = 10\nb = 20\n" + dash +
                            "\nfor cw              # ML620906\na = 30\n---------\nc = 5\n"
                            "def main():\n    pass\n";
    {
        std::string s_ii = wiz::select_sections(SRC, wiz::get_target("cwii"));
        std::string s_cw = wiz::select_sections(SRC, wiz::get_target("cw"));
        int n_ii = (int)std::count(s_ii.begin(), s_ii.end(), '\n');
        int n_cw = (int)std::count(s_cw.begin(), s_cw.end(), '\n');
        int n_src = (int)std::count(SRC.begin(), SRC.end(), '\n');
        check("sections: line count preserved", n_ii == n_src && n_src == n_cw);
        check("sections: cwii keeps its code, drops cw code",
              s_ii.find("a = 10") != std::string::npos && s_ii.find("b = 20") != std::string::npos &&
                  s_ii.find("a = 30") == std::string::npos);
        check("sections: cw keeps its code, drops cwii code",
              s_cw.find("a = 30") != std::string::npos && s_cw.find("a = 10") == std::string::npos &&
                  s_cw.find("b = 20") == std::string::npos);
        check("sections: common code before header and after separator",
              s_ii.find("x = 1") != std::string::npos && s_ii.find("c = 5") != std::string::npos &&
                  s_ii.find("def main") != std::string::npos &&
                  s_cw.find("x = 1") != std::string::npos && s_cw.find("c = 5") != std::string::npos &&
                  s_cw.find("def main") != std::string::npos);
        check("sections: header/separator lines blanked (em-dash never reaches the lexer)",
              s_ii.find("\xE2\x80\x94") == std::string::npos && s_ii.find("for ") == std::string::npos &&
                  s_cw.find("for ") == std::string::npos);
        std::vector<std::string> dt = wiz::declared_targets(SRC);
        check("declared targets in order", dt == std::vector<std::string>{"cwii", "cw"}, ss(dt));

        std::string multi = "for cwii, cw\nv = 1\n----\nfor all\nw = 2\n----\nz = 3\n";
        std::string mi = wiz::select_sections(multi, wiz::get_target("cw"));
        check("sections: \"for cwii, cw\" and \"for all\"",
              mi.find("v = 1") != std::string::npos && mi.find("w = 2") != std::string::npos &&
                  mi.find("z = 3") != std::string::npos);
        std::string ordinary =
            "n = 0\nfor i in range(3):\n    n += 1\ndef main():\n    pass\n";
        check("ordinary `for i in range` is not a header",
              wiz::select_sections(ordinary, wiz::get_target("cw")) == ordinary &&
                  wiz::declared_targets(ordinary).empty());

        bool ok = false;
        try {
            wiz::select_sections("for cw\nx = 1\n", wiz::get_target("cwii"));
        } catch (const wiz::SectionError& e) {
            ok = std::string(e.what()).find("no 'for cwii' section") != std::string::npos;
        }
        check("error: file without a section for the target", ok);

        ok = false;
        try {
            wiz::declared_targets("for ml620906\nx = 1\n");
        } catch (const wiz::SectionError& e) {
            std::string m = e.what();
            ok = m.find("unknown target") != std::string::npos && m.find("line 1") != std::string::npos;
        }
        check("error: unknown target name", ok);

        ok = false;
        try {
            wiz::build_source("for cw\nx = 1\ny = z\ndef main():\n    pass\n", "cw");
        } catch (const wiz::CompileError& e) {
            ok = (e.line == 3);
        }
        check("compiler error keeps the ORIGINAL line number inside a section", ok);
    }

    // ------------------------------------------------- one file, two machines
    const std::string DUAL = "which = 0\nfor cwii\nwhich = 2\nfor cw\nwhich = 1\n----\n"
                             "def main():\n    global which\n    which += 10\n";
    {
        Machine mi_ = run(DUAL, "cwii");
        Machine mc_ = run(DUAL, "cw");
        int vi = var_of(mi_.m, mi_.vars, "which"), vc = var_of(mc_.m, mc_.vars, "which");
        check("dual file: cwii build runs the cwii section", vi == 12, ex(vi, mi_.why));
        check("dual file: cw build runs the cw section", vc == 11, ex(vc, mc_.why));
        check("both stop in the halt loop",
              mi_.why.find("B to self") != std::string::npos &&
                  mc_.why.find("B to self") != std::string::npos,
              ex(mi_.why, mc_.why));
    }

    // ------------------------------------------------- memory map differences
    {
        const std::string BIG = "big = [0] * 3000\ndef main():\n    big[2999] = 7\n";
        wiz::build_source(BIG, "cwii");
        bool ok = false;
        try {
            wiz::build_source(BIG, "cw");
        } catch (const wiz::CompileError& e) {
            std::string m = e.what(), ml = wiz::to_lower(m);
            ok = m.find("RAM") != std::string::npos || ml.find("memory") != std::string::npos;
        }
        check("6000-byte array: fits cwii (20 KB vars), rejected on cw (4 KB vars)", ok);
        Machine r = run("big = [0] * 1900\ndef main():\n    big[1899] = 5\n    big[0] = 6\n", "cw");
        check("1900-element array fits cw and stays in RAM 0xD000-0xDFFF",
              r.m.mem[0xD000 + 2 * 1899] == 5 && r.m.mem[0xD000] == 6, r.why);
    }

    // ------------------------------------------------- boot translation sanity
    {
        const std::string TRIV = "def main():\n    pass\n";
        Machine mc_ = run(TRIV, "cw");
        Machine mi_ = run(TRIV, "cwii");
        check("cw boot reaches main and halts",
              mc_.why.find("B to self") != std::string::npos && mc_.m.sp == 0xF000,
              ex(mc_.why, mc_.m.sp));
        check("cw boot: LEA + [EA+] stores landed (IE0=20H, IE0+2=0)",
              mc_.m.mem[0xF010] == 0x20 && mc_.m.mem[0xF012] == 0);
        check("cw boot: final timer reload 05C8H (TM0D)",
              std::make_tuple(mc_.m.mem[0xF020], mc_.m.mem[0xF021]) == std::make_tuple(0xC8, 0x05));
        check("cw boot: FCON=01H after _reset_SFR, BLKCON0=17H",
              mc_.m.mem[0xF00A] == 0x01 && mc_.m.mem[0xF028] == 0x17,
              ex(mc_.m.mem[0xF00A], mc_.m.mem[0xF028]));
        int same[] = {0xF041, 0xF044, 0xF045, 0xF048, 0xF049, 0xF04A,
                      0xF04B, 0xF04C, 0xF220, 0xF221, 0xF222, 0xF223, 0xF224};
        std::vector<std::string> diff;
        for (int x : same)
            if (mc_.m.mem[x] != mi_.m.mem[x])
                diff.push_back("(" + wiz::fmt("%X", x) + ", " + std::to_string(mc_.m.mem[x]) + ", " +
                               std::to_string(mi_.m.mem[x]) + ")");
        check("cw boot port/key init equals the (independently translated) cwii boot", diff.empty(),
              ss(diff));
        check("cw boot never selects a bitplane (0F037H untouched)", mc_.m.mem[0xF037] == 0);
    }

    // ------------------------------------------------- 1-plane runtime
    std::string full_cw, full_ii;
    {
        const std::string TRIV = "def main():\n    pass\n";
        full_cw = run(TRIV, "cw").full;
        full_ii = run(TRIV, "cwii").full;
        auto cl = code_lines(full_cw);
        auto ci = code_lines(full_ii);
        int n_cw = 0, n_ii = 0;
        for (auto& l : cl)
            if (l.find("0F037H") != std::string::npos) n_cw++;
        for (auto& l : ci)
            if (l.find("0F037H") != std::string::npos) n_ii++;
        check("cw listing has no write to 0F037H; cwii listing has 4", n_cw == 0 && n_ii == 4,
              ex(n_cw, n_ii));
    }

    const std::string SCENE =
        "rom8 spr = [0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18]\n"
        "def draw():\n    clear()\n    rect(2, 2, 60, 30)\n    fillrect(10, 10, 20, 12)\n"
        "    hline(5, 100, 40)\n    vline(120, 3, 60)\n    line(0, 0, 191, 63)\n"
        "    circle(150, 30, 20)\n    disc(40, 50, 8)\n    text(70, 5, \"CW vs CWII\")\n"
        "    num(70, 20, -1234)\n    sprite(100, 45, 8, 8, spr)\n    pixel(11, 11, 0)\n"
        "    hline(0, 191, 63, 0)\n    pixel(191, 0)\n    pixel(300, 5)\n";
    const std::string direct = SCENE + "def main():\n    draw()\n";
    const std::string buffd = SCENE + "def main():\n    buffered(1)\n    draw()\n    flip()\n";
    {
        std::map<std::string, std::set<std::pair<int, int>>> imgs;
        for (const char* tg : {"cwii", "cw"}) {
            for (int kind = 0; kind < 2; kind++) {
                std::string src = kind ? buffd : direct;
                Machine r = run(src, tg);
                std::string key = std::string(tg) + "/" + (kind ? "buffered" : "direct");
                imgs[key] = black(r.m);
                check("scene " + key + " finished",
                      r.why.find("B to self") != std::string::npos && imgs[key].size() > 500,
                      ex(r.why, (int)imgs[key].size()));
            }
        }
        check("scene pixel-identical: cw == cwii on rows 0..62 (direct draw)",
              imgs["cw/direct"] == top63(imgs["cwii/direct"]));
        check("scene pixel-identical: cw == cwii on rows 0..62 (buffered + flip)",
              imgs["cw/buffered"] == top63(imgs["cwii/buffered"]));
        bool row63 = false;
        for (auto& p : imgs["cw/direct"])
            if (p.second >= 63) row63 = true;
        for (auto& p : imgs["cw/buffered"])
            if (p.second >= 63) row63 = true;
        check("cw draws nothing on row 63 (direct + buffered)", !row63);
        bool cwii63 = false;
        for (auto& p : imgs["cwii/direct"])
            if (p.second == 63) cwii63 = true;
        check("cwii still draws on row 63 (line to 191,63)", cwii63);
        check("scene: buffered == direct on cw", imgs["cw/buffered"] == imgs["cw/direct"]);
        auto& ref = imgs["cwii/direct"];
        bool sanity = ref.count({11, 11}) == 0 && ref.count({191, 0}) == 1;
        for (auto& p : ref) sanity = sanity && p.first < 192 && p.second < 64;
        check("scene sanity: erased pixel absent, corner pixel present, off-screen ignored", sanity);
    }

    // ------------------------------------------------- keys on cw
    {
        Machine r = run("const K_EXE = 5\nx = K_EXE\ny = K_UP\ndef main():\n    pass\n", "cw");
        int x = var_of(r.m, r.vars, "x"), y = var_of(r.m, r.vars, "y");
        check("cw: built-in K_EXE can be overridden once, others keep their value (K_UP = 40 on cw)",
              std::make_tuple(x, y) == std::make_tuple(5, 40), ex(x, y));
        Machine r2 = run("x = K_UP\ny = K_UP\ndef main():\n    pass\n", "cwii");
        check("cwii keeps its own key table (K_UP = 0x20)", var_of(r2.m, r2.vars, "x") == 0x20,
              std::to_string(var_of(r2.m, r2.vars, "x")));

        bool ok = false;
        try {
            wiz::build_source("const K_EXE = 5\nconst K_EXE = 6\ndef main():\n    pass\n", "cw");
        } catch (const wiz::CompileError& e) {
            ok = std::string(e.what()).find("already defined") != std::string::npos;
        }
        check("error: overriding the same K_ constant twice", ok);

        ok = false;
        try {
            wiz::build_source("const FOO = 1\nconst FOO = 2\ndef main():\n    pass\n", "cw");
        } catch (const wiz::CompileError& e) {
            ok = std::string(e.what()).find("already defined") != std::string::npos;
        }
        check("error: user constants still cannot be redefined", ok);

        for (auto pr : {std::make_pair(std::string("cw"), std::string("K_OK")),
                        std::make_pair(std::string("cwii"), std::string("K_ALPHA"))}) {
            const std::string& tgt = pr.first;
            const std::string& bad = pr.second;
            bool good = false;
            std::string msg;
            try {
                wiz::build_source("def main():\n    x = " + bad + "\n", tgt);
            } catch (const wiz::CompileError& e) {
                msg = e.what();
                good = (e.line == 2) && msg.find(bad) != std::string::npos &&
                       msg.find("not a key on " + tgt) != std::string::npos;
            }
            check(tgt + ": " + bad + " is rejected with a clear message", good, msg);
        }
        wiz::build_source(
            "# K_OK is only in a comment\nconst K_OK = 3\nx = K_OK\ndef main():\n    pass\n", "cw");
        check("cw: a user const named like a missing key is allowed", true);

        const auto& cwkeys = wiz::key_consts("cw");
        bool uniq = ((int)cwkeys.size() - 1) == 49;
        std::set<int> seen;
        for (auto& kv : cwkeys) {
            if (kv.first == "K_NONE") continue;
            if (!(kv.second >= 0 && kv.second < 64)) uniq = false;
            if (!seen.insert(kv.second).second) uniq = false;
        }
        check("cw key table: 49 keys, all scan codes unique and 0..63", uniq);
    }

    // ------------------------------------------------- command line
    {
        namespace fs = std::filesystem;
        fs::path tmp = fs::temp_directory_path() / ("wiztests_cw_" + std::to_string(::getpid()));
        fs::create_directories(tmp);
        wiz::write_text_file((tmp / "both.wz").string(), DUAL);
        wiz::write_text_file((tmp / "plain.wz").string(), "def main():\n    pass\n");
        std::string td = tmp.string();

        CliResult r = cli_run(td, {"both.wz", "--target", "880"});
        check("cli: 880 build of a sectioned file -> both.bin",
              r.rc == 0 && fs::exists(tmp / "both.bin"), r.out);
        r = cli_run(td, {"both.wz", "--target", "580", "-o", "both580.bin"});
        check("cli: 580 build -> both580.bin", r.rc == 0 && fs::exists(tmp / "both580.bin"), r.out);
        bool differ = false;
        {
            bool o1 = false, o2 = false;
            auto b1 = wiz::read_binary_file((tmp / "both.bin").string(), &o1);
            auto b2 = wiz::read_binary_file((tmp / "both580.bin").string(), &o2);
            differ = o1 && o2 && b1 != b2;
        }
        check("cli: the two .bin files differ", differ);
        r = cli_run(td, {"plain.wz"});
        check("cli: plain file -> plain.bin (default 880)", r.rc == 0 && fs::exists(tmp / "plain.bin"),
              r.out);
        r = cli_run(td, {"plain.wz", "--target", "580", "-o", "plain580.bin"});
        check("cli: --target 580 -> plain580.bin",
              r.rc == 0 && fs::exists(tmp / "plain580.bin"), r.out);
        r = cli_run(td, {"plain.wz", "--target", "999"});
        check("cli: unknown --target is an error",
              r.rc == 1 && r.out.find("unknown target") != std::string::npos, r.out);
    }

    // ------------------------------------------------- cw screen: 63 visible rows
    {
        const std::string OFFSRC =
            "def main():\n    clear()\n    pixel(0, 0)\n    pixel(0, 62)\n    pixel(191, 62)\n"
            "    pixel(0, 63)\n    hline(0, 192, 63)\n    while True:\n        pass\n";
        for (auto pr : {std::make_pair(std::string("cwii"), 0), std::make_pair(std::string("cw"), 1)}) {
            const std::string& tg = pr.first;
            int off = pr.second;
            Machine r = run(OFFSRC, tg, 3000000);
            auto row = [&](int n) { return vr(r.m, n); };
            {
                std::vector<int> r0 = row(0), ro = row(off);
                bool ok = ro[0] == 0x80;
                if (off != 0) {
                    bool any0 = false;
                    for (int v : r0) any0 = any0 || v;
                    ok = ok && !any0;
                }
                std::vector<int> head0(r0.begin(), r0.begin() + 3);
                std::vector<int> heado(ro.begin(), ro.begin() + 3);
                check(tg + ": user y=0 is VRAM row " + std::to_string(off), ok,
                      ex(off, head0, heado));
            }
            int last = 62 + off;
            check(tg + ": user y=62 is VRAM row " + std::to_string(last) + " (x=0 and x=191)",
                  row(last)[0] == 0x80 && row(last)[23] == 0x01);
            if (tg == "cw") {
                int cnt = 0;
                for (int i = 0; i < 2048; i++)
                    if (r.m.planes[0][i]) cnt++;
                check("cw: y=63 (pixel + hline) draws nothing", cnt == 3, std::to_string(cnt));
            } else {
                std::vector<int> r63 = row(63);
                bool any = false;
                for (int v : r63) any = any || v;
                std::vector<int> head(r63.begin(), r63.begin() + 4);
                check("cwii: y=63 is a real row (pixel + hline draw there)", any, ss(head));
            }
        }
    }
}
