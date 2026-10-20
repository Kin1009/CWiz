// test_v03b.cpp - Wiz 0.3b tests: print, fillrect model, pong, demo
// (ported from tests/test_v03b.py).
#include <random>
#include <set>
#include <string>

#include "font6x8.hpp"
#include "harness.hpp"

using namespace wt;

namespace {

Machine build_and_run(const std::string& src, std::vector<std::pair<int, int>> keys = {},
                      long steps = 30000000) {
    return run_src(src, "", std::move(keys), steps);
}

std::set<std::pair<int, int>> text_ref(int x, int y, const std::string& s) {
    std::set<std::pair<int, int>> out;
    for (size_t i = 0; i < s.size(); i++) {
        int base = ((int)(unsigned char)s[i] - 32) * 8;
        for (int r = 0; r < 8; r++)
            for (int k = 0; k < 6; k++)
                if (FONT6X8[base + r] & (0x80 >> k))
                    out.insert({x + (int)i * 6 + k, y + r});
    }
    return out;
}

bool subset(const std::set<std::pair<int, int>>& a, const std::set<std::pair<int, int>>& b) {
    for (auto& p : a)
        if (!b.count(p)) return false;
    return true;
}

}  // namespace

void wt::run_v03b() {
    // ---------------- print
    {
        std::string src = "\nrom8 name = \"Bob\"\ndef main():\n    score = 42\n"
                          "    print(2, 3, \"Score \", score, \" of \", -7)\n"
                          "    print(2, 20, name)\n    e = print(2, 40, 123)\n"
                          "    print(100, 40, e)\n";
        Machine r = build_and_run(src);
        std::set<std::pair<int, int>> exp = text_ref(2, 3, "Score 42 of -7");
        for (auto& p : text_ref(2, 20, "Bob")) exp.insert(p);
        for (auto& p : text_ref(2, 40, "123")) exp.insert(p);
        for (auto& p : text_ref(100, 40, std::to_string(2 + 18))) exp.insert(p);
        auto got = lit(r.m);
        std::set<std::pair<int, int>> sym;
        std::set_symmetric_difference(got.begin(), got.end(), exp.begin(), exp.end(),
                                      std::inserter(sym, sym.begin()));
        check("print: strings + numbers chained, rom8 name as text, returns end x", got == exp,
              ss(sym));
    }

    // ---------------- asm hline / fillrect vs python model, both draw modes
    {
        std::mt19937 rng(5);
        std::uniform_int_distribution<int> dx(-10, 200), dw(0, 120), dy(-3, 66), dh(1, 6),
            dc(0, 1);
        std::vector<std::array<int, 5>> ops;
        for (int i = 0; i < 60; i++)
            ops.push_back({dx(rng), dy(rng), dw(rng), dh(rng), dc(rng)});
        int extra[10][5] = {{0, 0, 192, 1, 1},  {191, 5, 1, 1, 1}, {0, 6, 8, 1, 1},
                            {3, 7, 5, 1, 1},    {8, 8, 8, 1, 1},   {7, 9, 2, 1, 1},
                            {0, 10, 192, 1, 1}, {5, 10, 100, 1, 0}, {64, 11, 128, 1, 1},
                            {63, 12, 1, 1, 1}};
        for (auto& e : extra) ops.push_back({e[0], e[1], e[2], e[3], e[4]});

        auto model = [&](const std::vector<std::array<int, 5>>& o) {
            std::set<std::pair<int, int>> g;
            for (auto& t : o) {
                int x = t[0], y = t[1], w = t[2], h = t[3], c = t[4];
                for (int j = y; j < y + h; j++) {
                    if (!(j >= 0 && j < 64)) continue;
                    for (int i = x; i < x + w; i++)
                        if (i >= 0 && i < 192) {
                            if (c) g.insert({i, j});
                            else g.erase({i, j});
                        }
                }
            }
            return g;
        };
        std::set<std::pair<int, int>> exp = model(ops);

        for (int mode = 0; mode < 2; mode++) {
            std::string body = "def main():\n";
            if (mode) body += "    buffered(1)\n";
            for (auto& t : ops)
                body += "    fillrect(" + std::to_string(t[0]) + ", " + std::to_string(t[1]) + ", " +
                        std::to_string(t[2]) + ", " + std::to_string(t[3]) + ", " +
                        std::to_string(t[4]) + ")\n";
            Machine r = build_and_run(body, {}, 80000000);
            auto got = mode ? fb(r.m) : lit(r.m);
            std::set<std::pair<int, int>> sym;
            std::set_symmetric_difference(got.begin(), got.end(), exp.begin(), exp.end(),
                                          std::inserter(sym, sym.begin()));
            check(std::string("hline/fillrect asm span == model (") +
                      (mode ? "buffered" : "direct") + " mode, " + std::to_string(ops.size()) +
                      " ops)",
                  got == exp, ex((int)got.size(), (int)exp.size(), sym, r.why));
            if (!mode) {
                bool both = true;
                for (auto& p : got) both = both && (r.m.pixel(p.first, p.second) == 3);
                check("direct mode sets/clears BOTH bitplanes", both);
            }
        }
    }

    const auto& KC = key_consts_default();

    // ---------------- pong
    {
        std::string text = wiz::read_text_file(root_path("examples/05_pong.wz"));
        Machine r = run_src(text, "", {}, 30000000);
        const auto& V = r.vars;
        auto g = [&](const std::string& n) { return var_of(r.m, V, n); };

        std::set<std::pair<int, int>> top_violations;
        for (int i = 0; i < 120; i++) {
            if (i == 30) r.m.keys = {key_pair(KC.at("K_DOWN"))};
            if (i == 60) r.m.keys = {key_pair(KC.at("K_UP"))};
            if (i == 90) r.m.keys.clear();
            r.m.run(r.m.steps + 300000);
            for (auto& p : lit(r.m))
                if (p.second < 8) top_violations.insert(p);
        }
        std::vector<std::pair<int, int>> tv(top_violations.begin(), top_violations.end());
        check("pong never lights a pixel in the top 8 rows (status bar area)", top_violations.empty(),
              ss(tv));
        bool border = true;
        for (int x = 0; x < 192; x++) border = border && (r.m.pixel(x, 8) == 3);
        for (int x = 0; x < 192; x++) border = border && (r.m.pixel(x, 63) == 3);
        check("pong draws the field border at y=TOP", border);
        check("pong keys: paddle clamped inside field", 10 <= g("py") && g("py") <= 48,
              std::to_string(g("py")));
        // force a win and check the message renders
        r.m.mem[V.at("me")] = 8;
        r.m.mem[V.at("me") + 1] = 0;
        r.m.run(r.m.steps + 1500000);
        auto exp = text_ref(72, 33 - 4, "YOU WIN!");
        int missing = 0;
        for (auto& p : exp)
            if (!lit(r.m).count(p)) missing++;
        check("pong shows \"YOU WIN!\" at 8 points", subset(exp, lit(r.m)), std::to_string(missing));
    }

    // ---------------- demo 06
    {
        std::string text = wiz::read_text_file(root_path("examples/06_demo.wz"));
        Machine r = run_src(text, "", {}, 8000000);
        check("demo: n=5 -> fib=5, 5!=120 shown",
              subset(text_ref(4, 11, "n = 5"), lit(r.m)) &&
                  subset(text_ref(4, 21, "fib = 5"), lit(r.m)) &&
                  subset(text_ref(4, 31, "n! = 120"), lit(r.m)));
        r.m.keys = {key_pair(KC.at("K_UP"))};
        r.m.run(r.m.steps + 6000000);
        r.m.keys.clear();
        r.m.run(r.m.steps + 6000000);
        check("demo: UP -> n=6, fib=8, 6!=720, presses=1",
              subset(text_ref(4, 11, "n = 6"), lit(r.m)) &&
                  subset(text_ref(4, 21, "fib = 8"), lit(r.m)) &&
                  subset(text_ref(4, 31, "n! = 720"), lit(r.m)) &&
                  subset(text_ref(4, 41, "presses: 1"), lit(r.m)));
    }
}
