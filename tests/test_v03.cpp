// test_v03.cpp - Wiz 0.3 tests: arrays, rom tables, strings, text/num, sprite,
// recursion, peek/poke, errors (ported from tests/test_v03.py).
#include <set>
#include <string>

#include "font6x8.hpp"
#include "harness.hpp"

using namespace wt;

namespace {

const wiz::Target& TG() {
    static const wiz::Target& t = wiz::get_target(wiz::DEFAULT_TARGET);
    return t;
}
int PK() { return TG().var_base + 0xF00; }  // spare RAM address (0x9F00 cwii, 0xDF00 cw)

Machine run(const std::string& src, long steps = 6000000) {
    std::string s = wiz::replace_all(src, "@PK", wiz::fmt("0x%X", PK()));
    return run_src(s, "", {}, steps);
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
    std::set<std::pair<int, int>> clipped;
    for (auto& p : out)
        if (p.first >= 0 && p.first < 192 && p.second >= 0 && p.second < 64) clipped.insert(p);
    return clipped;
}

std::string errs(const std::string& src) {
    try {
        wiz::build_source(src, "");
    } catch (const std::exception& e) {
        return e.what();
    }
    return "";
}

}  // namespace

void wt::run_v03() {
    // ------------------------------------------------------------------ arrays
    {
        Machine r = run("\na = [10, 20, 30, 40]\nz = [0] * 20\nbig = [7] * 30\ntotal = 0\ni = 0\n"
                        "t1 = 0\nt2 = 0\nt3 = 0\nt4 = 0\n"
                        "def main():\n    global total, t1, t2, t3, t4\n"
                        "    for i in range(0, 4):\n        total += a[i]\n"
                        "    a[1] = 99\n    a[2] += 5\n    a[3] -= 1\n    z[19] = 1234\n"
                        "    z[0] = a[1] + a[2]\n    t1 = a[a[0] - 9]\n"
                        "    t2 = z[19] + big[29] + big[0]\n    t3 = a[2]\n    k = 3\n"
                        "    z[k + 2] = k * 100\n    t4 = z[5]\n");
        std::vector<int> got = {var_of(r.m, r.vars, "total"), var_of(r.m, r.vars, "t1"),
                                var_of(r.m, r.vars, "t2"), var_of(r.m, r.vars, "t3"),
                                var_of(r.m, r.vars, "t4")};
        std::vector<int> want = {100, 99, 1234 + 14, 35, 300};
        check("array init, read, write, +=, nested index", got == want, ss(got));

        int base = TG().var_base;
        int w0 = r.m.mem[base] | (r.m.mem[base + 1] << 8);
        int w1 = r.m.mem[base + 2] | (r.m.mem[base + 3] << 8);
        check("array memory layout words", w0 == 10 && w1 == 99, ex(r.m.mem[base], r.m.mem[base + 2]));
    }

    // ------------------------------------------------------------------ rom tables
    {
        Machine r = run("\nrom w = [100, -5, 30000, 7]\nrom8 b = [1, 2, 255, 128]\nrom8 msg = \"Hi!\"\n"
                        "r0 = w[0]\nr1 = w[1]\nr2 = w[2]\nb0 = b[0]\nb2 = b[2]\nb3 = b[3]\n"
                        "c0 = msg[0]\nc2 = msg[2]\nc3 = msg[3]\np = peek(msg + 1)\n"
                        "def f(i):\n    return w[i] + b[i]\nr5 = f(3)\ndef main():\n    pass\n");
        const char* names[] = {"r0", "r1", "r2", "b0", "b2", "b3", "c0", "c2", "c3", "p", "r5"};
        std::vector<int> got;
        for (auto* n : names) got.push_back(var_of(r.m, r.vars, n));
        std::vector<int> want = {100, -5, 30000, 1, 255, 128, 72, 33, 0, 105, 7 + 128};
        check("rom/rom8 tables, strings, peek on ROM", got == want, ss(got));
    }

    // ------------------------------------------------------------------ text / num rendering
    {
        Machine r = run("\ndef main():\n    text(3, 5, \"Hello, World! 0123456789\")\n"
                        "    text(100, 40, \"A~z\")\n    text(-4, 60, \"clip\")\n    render()\n");
        auto exp = text_ref(3, 5, "Hello, World! 0123456789");
        for (auto& p : text_ref(100, 40, "A~z")) exp.insert(p);
        for (auto& p : text_ref(-4, 60, "clip")) exp.insert(p);
        auto got = lit(r.m);
        check("text() matches font reference (incl. clipping)", got == exp,
              ex((int)got.size(), (int)exp.size(), lit(r.m), r.why));
    }
    {
        std::vector<int> cases = {0, 7, 10, 99, 100, 12345, -5, -32768, 32767, -1000, 1, -1};
        std::string prog = "def main():\n";
        for (size_t i = 0; i < cases.size(); i++)
            prog += "    num(" + std::to_string(4 + (int)(i % 2) * 90) + ", " +
                    std::to_string(2 + (int)(i / 2) * 10) + ", " + std::to_string(cases[i]) + ")\n";
        prog += "    render()\n";
        Machine r = run(prog, 30000000);
        std::set<std::pair<int, int>> exp;
        for (size_t i = 0; i < cases.size(); i++)
            for (auto& p : text_ref(4 + (int)(i % 2) * 90, 2 + (int)(i / 2) * 10,
                                    std::to_string(cases[i])))
                exp.insert(p);
        auto got = lit(r.m);
        std::set<std::pair<int, int>> sym;
        std::set_symmetric_difference(got.begin(), got.end(), exp.begin(), exp.end(),
                                      std::inserter(sym, sym.begin()));
        check("num() renders signed decimals incl. -32768 / 32767 / 0", got == exp, ex(sym, r.why));
    }
    {
        Machine r = run("\ne = text(10, 10, \"abcd\")\nf = num(10, 30, -123)\ndef main():\n    pass\n");
        int e = var_of(r.m, r.vars, "e"), f = var_of(r.m, r.vars, "f");
        check("text/num return end x", std::make_tuple(e, f) == std::make_tuple(34, 34), ex(e, f));
    }

    // ------------------------------------------------------------------ sprite
    {
        Machine r = run("\nrom8 arrow = [0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18,   0xAA, 0x55]\n"
                        "def main():\n    sprite(20, 10, 8, 8, arrow)\n    sprite(60, 30, 10, 1, arrow + 8)\n    render()\n");
        std::set<std::pair<int, int>> exp;
        int data[8] = {0x18, 0x3C, 0x7E, 0xFF, 0x18, 0x18, 0x18, 0x18};
        for (int rr = 0; rr < 8; rr++)
            for (int k = 0; k < 8; k++)
                if (data[rr] & (0x80 >> k)) exp.insert({20 + k, 10 + rr});
        for (int k = 0; k < 10; k++) {
            int byte = (k >> 3) ? 0x55 : 0xAA;
            if (byte & (0x80 >> (k & 7))) exp.insert({60 + k, 30});
        }
        auto got = lit(r.m);
        std::set<std::pair<int, int>> sym;
        std::set_symmetric_difference(got.begin(), got.end(), exp.begin(), exp.end(),
                                      std::inserter(sym, sym.begin()));
        check("sprite() 8x8 and 10-wide rows", got == exp, ss(sym));
    }

    // ------------------------------------------------------------------ recursion
    {
        Machine r = run("\ndef fact(n):\n    if n <= 1:\n        return 1\n    return n * fact(n - 1)\n"
                        "def fib(n):\n    if n < 2:\n        return n\n    return fib(n - 1) + fib(n - 2)\n"
                        "def gcd(a, b):\n    if b == 0:\n        return a\n    return gcd(b, a % b)\n"
                        "def is_even(n):\n    if n == 0:\n        return 1\n    return is_odd(n - 1)\n"
                        "def is_odd(n):\n    if n == 0:\n        return 0\n    return is_even(n - 1)\n"
                        "def ack(m, n):\n    if m == 0:\n        return n + 1\n    if n == 0:\n"
                        "        return ack(m - 1, 1)\n    return ack(m - 1, ack(m, n - 1))\n"
                        "def sumto(n):\n    s = 0\n    for i in range(0, 1):\n        if n > 0:\n"
                        "            s = n + sumto(n - 1)\n    return s\n"
                        "def depth(n):\n    k = n * 2\n    if n == 0:\n        return 0\n"
                        "    r = depth(n - 1)\n    return r + k - n * 2 + 1\n"
                        "r_fact = fact(7)\nr_fib = fib(12)\nr_gcd = gcd(1071, 462)\n"
                        "r_even = is_even(11) * 10 + is_odd(11)\nr_ack = ack(2, 3)\n"
                        "r_sum = sumto(60)\nr_depth = depth(150)\ndef main():\n    pass\n",
                        60000000);
        const char* names[] = {"r_fact", "r_fib", "r_gcd", "r_even", "r_ack", "r_sum", "r_depth"};
        std::vector<int> got;
        for (auto* n : names) got.push_back(var_of(r.m, r.vars, n));
        std::vector<int> want = {5040, 144, 21, 1, 9, 1830, 150};
        check("recursion: fact, fib, gcd, mutual even/odd, ackermann, loops-in-recursion, depth 150",
              got == want, ss(got));

        std::vector<std::string> touched;
        for (int a = 0xE000; a < 0xE810; a++)
            if (r.m.mem[a]) touched.push_back(wiz::fmt("%X", a));
        check("stack stayed clear of runtime RAM (0xE000..0xE80F untouched)", touched.empty(),
              ss(touched));
    }

    // ------------------------------------------------------------------ peek / poke
    {
        Machine r = run("\npoke(@PK, 77)\npoke(@PK + 1, 300)\na = peek(@PK)\nb = peek(@PK + 1)\n"
                        "c = peek(@PK) + peek(@PK + 1) * 256\ndef main():\n    pass\n");
        int a = var_of(r.m, r.vars, "a"), b = var_of(r.m, r.vars, "b"), c = var_of(r.m, r.vars, "c");
        check("poke/peek roundtrip (low byte only)",
              std::make_tuple(a, b, c) == std::make_tuple(77, 44, 77 + 44 * 256), ex(a, b, c));
    }

    // ------------------------------------------------------------------ error cases
    {
        std::string e1 = errs("rom t = [1,2]\ndef main():\n    t[0] = 5\n");
        check("error: write to rom table", !e1.empty() && e1.find("read-only") != std::string::npos, e1);
        std::string e2 = errs("a = [1,2]\na = 5\ndef main():\n    pass\n");
        check("error: assign to array name", !e2.empty() && e2.find("array") != std::string::npos, e2);
        std::string e3 = errs("def main():\n    x[0] = 1\n");
        check("error: unknown array",
              !e3.empty() && e3.find("not a RAM array") != std::string::npos, e3);
        std::string e4 = errs("rom8 t = [1, 300]\ndef main():\n    pass\n");
        check("error: rom8 entry out of range", !e4.empty() && e4.find("0..255") != std::string::npos,
              e4);
        std::string e5 = errs("def main():\n    text(0, 0, \"caf\\xe9\")\n");
        check("error: bad escape/nonascii string", !e5.empty(), e5);
        std::string e6 = errs("a = [0] * 4\ndef f():\n    return 1\ndef main():\n    a[f()] += 1\n");
        check("error: call inside index of compound assignment",
              !e6.empty() && e6.find("function call") != std::string::npos, e6);
        std::string e7 = errs("def main():\n    x = \"hi\" \"there\"\n");
        check("error: junk after string", !e7.empty(), e7);
    }
}
