// test_v02.cpp - Wiz 0.2 regression tests (ported from tests/test_v02.py).
#include <algorithm>
#include <set>

#include "harness.hpp"

using namespace wt;

namespace {

Machine run(const std::string& src, std::vector<std::pair<int, int>> keys = {},
            long steps = 3000000) {
    return run_src(src, "", std::move(keys), steps);
}

}  // namespace

void wt::run_v02() {
    // ------------------------------------------------------------ 1. constants
    {
        Machine r = run("\nconst W = 192\nconst H = W / 3\nconst MASK = 1 << 4\n"
                        "a = W\nb = H\nc = MASK\nd = K_OK\ne = K_NONE\n"
                        "def main():\n    pass\n");
        int a = var_of(r.m, r.vars, "a"), b = var_of(r.m, r.vars, "b"),
            c = var_of(r.m, r.vars, "c"), d = var_of(r.m, r.vars, "d"),
            e = var_of(r.m, r.vars, "e");
        check("const folding", std::make_tuple(a, b, c, d, e) == std::make_tuple(192, 64, 16, 0x21, -1),
              ex(a, b, c, d, e));
    }

    // ------------------------------------------------------------ 2. pixel clipping
    {
        Machine r = run("\nsentinel = 4242\ndef main():\n    pixel(-1, 5)\n    pixel(192, 5)\n"
                        "    pixel(300, 5)\n    pixel(5, 64)\n    pixel(5, -3)\n    pixel(5, 200)\n"
                        "    pixel(0, 0)\n    pixel(191, 63)\n");
        auto got = lit(r.m);
        std::set<std::pair<int, int>> want = {{0, 0}, {191, 63}};
        check("pixel clipping direct mode",
              got == want && var_of(r.m, r.vars, "sentinel") == 4242, ss(got));
    }
    {
        Machine r = run("\nsentinel = 4242\ndef main():\n    buffered(1)\n    pixel(-1, 5)\n"
                        "    pixel(192, 5)\n    pixel(5, 64)\n    pixel(5, -3)\n    pixel(0, 0)\n"
                        "    pixel(191, 63)\n    pixel(5, 5)\n    pixel(5, 5, 0)\n");
        auto got = fb(r.m);
        std::set<std::pair<int, int>> want = {{0, 0}, {191, 63}};
        check("pixel clipping buffered mode",
              got == want && var_of(r.m, r.vars, "sentinel") == 4242, ss(got));
        check("buffered draws NOT visible before flip", lit(r.m).empty(), ss(lit(r.m)));
    }

    // ------------------------------------------------------------ 3. flip
    {
        Machine r = run("\ndef main():\n    buffered(1)\n    pixel(10, 10)\n    pixel(100, 40)\n"
                        "    flip()\n    pixel(50, 50)\n    clear()\n    pixel(7, 7)\n");
        auto l = lit(r.m);
        bool all3 = true;
        for (auto& p : l) all3 = all3 && (r.m.pixel(p.first, p.second) == 3);
        std::set<std::pair<int, int>> want = {{10, 10}, {100, 40}};
        check("flip shows the frame (both planes, black)", l == want && all3, ss(l));
        auto f = fb(r.m);
        std::set<std::pair<int, int>> fwant = {{7, 7}};
        check("buffer cleared after flip+clear", f == fwant, ss(f));
    }

    // ------------------------------------------------------------ 4. key_down / key_hit
    const auto& KC = key_consts_default();
    {
        std::string src =
            "\nr_ok = 0\nr_up = 0\nh1 = 0\nh2 = 0\nh3 = 0\nh4 = 0\n"
            "def main():\n    global r_ok, r_up, h1, h2, h3, h4\n"
            "    r_ok = key_down(K_OK)\n    r_up = key_down(K_UP)\n"
            "    h1 = key_hit(K_OK)\n    h2 = key_hit(K_OK)\n    delay(1)\n"
            "    h3 = key_hit(K_OK)\n    while True:\n        pass\n";
        Machine r = run(src, {key_pair(KC.at("K_OK"))}, 60000);
        check("key_down true for held key", var_of(r.m, r.vars, "r_ok") == 1);
        check("key_down false for other key", var_of(r.m, r.vars, "r_up") == 0);
        int h1 = var_of(r.m, r.vars, "h1"), h2 = var_of(r.m, r.vars, "h2"),
            h3 = var_of(r.m, r.vars, "h3");
        check("key_hit first call = 1, second = 0 while held",
              std::make_tuple(h1, h2, h3) == std::make_tuple(1, 0, 0), ex(h1, h2, h3));
    }

    // all 64 scan indexes round-trip
    {
        const char* tmpl =
            "\nk = %s\nres = 0\nother = 0\ndef main():\n    global res, other\n"
            "    res = key_down(k)\n    other = 0\n    for j in range(0, 64):\n"
            "        if j != k:\n            other += key_down(j)\n"
            "    while True:\n        pass\n";
        std::vector<std::string> bad;
        for (int k = 0; k < 64; k++) {
            std::string src = tmpl;
            size_t pos = src.find("%s");
            src.replace(pos, 2, std::to_string(k));
            Machine r = run(src, {key_pair(k)}, 400000);
            int res = var_of(r.m, r.vars, "res"), other = var_of(r.m, r.vars, "other");
            if (res != 1 || other != 0)
                bad.push_back("(" + std::to_string(k) + ", " + std::to_string(res) + ", " +
                              std::to_string(other) + ")");
        }
        check("key_down exact for all 64 scan indexes", bad.empty(), ss(bad));
    }

    // key_hit across press / release / press
    {
        std::string src =
            "\nlog = 0\ndef main():\n    global log\n    log = 0\n    while True:\n"
            "        if key_hit(K_EXE):\n            log += 1\n";
        Machine r = run(src, {}, 20000);
        int c0 = var_of(r.m, r.vars, "log");
        r.m.keys = {key_pair(KC.at("K_EXE"))};
        r.m.run(r.m.steps + 20000);
        int c1 = var_of(r.m, r.vars, "log");
        r.m.run(r.m.steps + 20000);
        int c2 = var_of(r.m, r.vars, "log");
        r.m.keys.clear();
        r.m.run(r.m.steps + 20000);
        r.m.keys = {key_pair(KC.at("K_EXE"))};
        r.m.run(r.m.steps + 20000);
        int c3 = var_of(r.m, r.vars, "log");
        check("key_hit: 0 idle, 1 on press, still 1 while held, 2 after release+press",
              std::make_tuple(c0, c1, c2, c3) == std::make_tuple(0, 1, 1, 2),
              ex(c0, c1, c2, c3));
    }

    // ------------------------------------------------------------ 5. drawing library vs python reference
    {
        auto ref_line = [](int x0, int y0, int x1, int y1) {
            std::vector<std::pair<int, int>> pts;
            int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
            int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int err = dx + dy;
            while (true) {
                pts.push_back({x0, y0});
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
            return pts;
        };
        auto ref_circle = [](int cx, int cy, int rr) {
            std::set<std::pair<int, int>> pts;
            int x = rr, y = 0, err = 1 - rr;
            while (x >= y) {
                const int d[8][2] = {{x, y},   {y, x},   {-y, x},  {-x, y},
                                     {-x, -y}, {-y, -x}, {y, -x},  {x, -y}};
                for (auto& p : d) pts.insert({cx + p[0], cy + p[1]});
                y += 1;
                if (err < 0) err += 2 * y + 1;
                else { x -= 1; err += 2 * (y - x) + 1; }
            }
            return pts;
        };

        std::vector<std::array<int, 4>> lines = {{2, 3, 150, 40}, {150, 40, 2, 3}, {10, 60, 10, 5},
                                                 {0, 0, 191, 0},   {30, 30, 30, 30}, {(-20), 10, 60, 50},
                                                 {100, 70, 120, (-5)}};
        std::string prog = "def main():\n    buffered(1)\n";
        for (auto& l : lines)
            prog += "    line(" + std::to_string(l[0]) + ", " + std::to_string(l[1]) + ", " +
                    std::to_string(l[2]) + ", " + std::to_string(l[3]) + ")\n";
        prog += "    circle(96, 32, 20)\n    circle(10, 10, 15)\n    rect(5, 5, 30, 20)\n"
                "    fillrect(150, 40, 10, 8)\n    disc(60, 50, 6)\n";

        Machine r = run(prog, {}, 20000000);
        std::set<std::pair<int, int>> exp;
        for (auto& l : lines)
            for (auto& p : ref_line(l[0], l[1], l[2], l[3])) exp.insert(p);
        for (auto& p : ref_circle(96, 32, 20)) exp.insert(p);
        for (auto& p : ref_circle(10, 10, 15)) exp.insert(p);
        for (int x = 5; x < 35; x++) { exp.insert({x, 5}); exp.insert({x, 24}); }
        for (int y = 5; y < 25; y++) { exp.insert({5, y}); exp.insert({34, y}); }
        for (int x = 150; x < 160; x++)
            for (int y = 40; y < 48; y++) exp.insert({x, y});
        for (int dx = -6; dx <= 6; dx++)
            for (int dy = -6; dy <= 6; dy++)
                if (dx * dx + dy * dy <= 36) exp.insert({60 + dx, 50 + dy});
        std::set<std::pair<int, int>> clipped;
        for (auto& p : exp)
            if (p.first >= 0 && p.first < 192 && p.second >= 0 && p.second < 64) clipped.insert(p);
        exp = clipped;

        auto got = fb(r.m);
        std::set<std::pair<int, int>> sym;
        std::set_symmetric_difference(got.begin(), got.end(), exp.begin(), exp.end(),
                                      std::inserter(sym, sym.begin()));
        check("line/circle/rect/fillrect/disc match Python reference (incl. clipping)", got == exp,
              ex((int)got.size(), (int)exp.size(), sym, r.why));
    }

    // ------------------------------------------------------------ 6. math + rand + defaults
    {
        Machine r = run("\na = abs(-5)\nb = min(3, 9)\nc = max(3, 9)\nd = clamp(50, 0, 10)\n"
                        "e = sign(-77)\nsrand(1)\nr1 = rand(1000)\nr2 = rand(1000)\nr3 = rand(1000)\n"
                        "def f(x, y=10, z=20):\n    return x + y + z\ng1 = f(1)\ng2 = f(1, 2)\n"
                        "g3 = f(1, 2, 3)\ndef main():\n    pass\n");
        std::vector<int> abcde = {var_of(r.m, r.vars, "a"), var_of(r.m, r.vars, "b"),
                                  var_of(r.m, r.vars, "c"), var_of(r.m, r.vars, "d"),
                                  var_of(r.m, r.vars, "e")};
        check("abs/min/max/clamp/sign", abcde == std::vector<int>{5, 3, 9, 10, -1}, ss(abcde));

        int seed = 1;
        std::vector<int> rs;
        for (int i = 0; i < 3; i++) {
            seed = (seed * 25173 + 13849) & 0xFFFF;
            rs.push_back(((seed >> 1) & 0x7FFF) % 1000);
        }
        std::vector<int> got = {var_of(r.m, r.vars, "r1"), var_of(r.m, r.vars, "r2"),
                                var_of(r.m, r.vars, "r3")};
        check("rand matches Python LCG", got == rs, ex(got, rs));

        std::vector<int> g = {var_of(r.m, r.vars, "g1"), var_of(r.m, r.vars, "g2"),
                              var_of(r.m, r.vars, "g3")};
        check("default arguments", g == std::vector<int>{31, 23, 6}, ss(g));
    }

    // user function overrides library function of same name
    {
        Machine r = run("\ndef abs(n):\n    return 123\na = abs(-5)\ndef main():\n    pass\n");
        check("user def overrides library def", var_of(r.m, r.vars, "a") == 123,
              std::to_string(var_of(r.m, r.vars, "a")));
    }
}
