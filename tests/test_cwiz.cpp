// test_cwiz.cpp - CWiz language tests (ported from tests/test_cwiz.py).
#include <string>

#include "font_cw.hpp"
#include "harness.hpp"

using namespace wt;

namespace {

Machine run(const std::string& src, const std::string& target = "cwii", long steps = 400000,
            std::vector<std::pair<int, int>> keys = {}) {
    wiz::BuildResult br = wiz::build_cwiz(src, target);
    wiz::U8 m(br.rom, std::move(keys), target);
    m.run(steps);
    return Machine{std::move(m), br.variables, "", br.rom, br.full_asm, br.syms};
}

int pix1(const wiz::U8& m, int x, int y) { return m.pixel(x, y) ? 1 : 0; }

std::set<std::pair<int, int>> cw_ref(int x, int y, const std::string& s) {
    std::set<std::pair<int, int>> out;
    for (size_t i = 0; i < s.size(); i++) {
        int cp = (unsigned char)s[i];
        if (cp < FONT_CW_FIRST || cp > FONT_CW_LAST) cp = '?';
        int base = (cp - FONT_CW_FIRST) * FONT_CW_H;
        for (int r = 0; r < FONT_CW_H; r++)
            for (int k = 0; k < FONT_CW_W; k++)
                if (FONT_CW[base + r] & (1 << (FONT_CW_W - 1 - k)))
                    out.insert({x + (int)i * FONT_CW_W + k, y + r});
    }
    return out;
}

struct ErrRes {
    bool ok;
    std::string msg;
};

ErrRes expect_error(const std::string& src, const std::string& needle,
                    const std::string& target = "cwii") {
    try {
        wiz::build_cwiz(src, target);
    } catch (const std::exception& e) {
        std::string s = e.what();
        return {s.find(needle) != std::string::npos, s};
    }
    return {false, "no error"};
}

}  // namespace

void wt::run_cwiz() {
    // --------------------------------------------------------------------- expressions
    {
        Machine r = run("int R; void main() { R = 2 + 3 * 4 - 6 / 2; }");
        check("arithmetic precedence  2+3*4-6/2 = 11", var_of(r.m, r.vars, "R") == 11,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = 7 % 3; }");
        check("remainder  7%3 = 1", var_of(r.m, r.vars, "R") == 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = -7 % 3; }");
        check("remainder  -7%3 = -1 (C semantics)", var_of(r.m, r.vars, "R") == -1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = (5 << 3) | (0x0F & 0x03); }");
        check("shift + bitwise", var_of(r.m, r.vars, "R") == ((5 << 3) | (0x0F & 0x03)),
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = (3 < 5) + (5 < 3) + (4 == 4) + (4 != 4); }");
        check("comparisons yield 0/1", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = (1 && 0) + (1 || 0) + (!0) + (!5); }");
        check("logical && || !", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = 5 > 3 ? 100 : 200; }");
        check("ternary ?:", var_of(r.m, r.vars, "R") == 100, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int x = 10; R = -x + ~0 + +x; }");
        check("unary - ~ +", var_of(r.m, r.vars, "R") == -1, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- control flow
    {
        Machine r = run("int R; void main() { int i; for (i = 0; i < 10; i = i + 1) { R = R + i; } }");
        check("for loop sum 0..9 = 45", var_of(r.m, r.vars, "R") == 45,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R = 1; void main() { int i = 0; while (i < 5) { R = R * 2; i = i + 1; } }");
        check("while loop 1<<5 = 32", var_of(r.m, r.vars, "R") == 32,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int i = 0; do { R = R + 3; i = i + 1; } while (i < 4); }");
        check("do-while runs 4 times", var_of(r.m, r.vars, "R") == 12,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int i; for (i = 0; i < 20; i = i + 1) "
                        "{ if (i == 5) { break; } R = R + 1; } }");
        check("break exits the loop at i==5", var_of(r.m, r.vars, "R") == 5,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int i; for (i = 0; i < 10; i = i + 1) "
                        "{ if (i % 2 == 0) { continue; } R = R + i; } }");
        check("continue skips even i (1+3+5+7+9=25)", var_of(r.m, r.vars, "R") == 25,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int i = 0; while (1) { i = i + 1; if (i > 7) { break; } } R = i; }");
        check("while(1) + break", var_of(r.m, r.vars, "R") == 8,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { if (0) { R = 1; } else if (0) { R = 2; } else { R = 3; } }");
        check("if / else if / else", var_of(r.m, r.vars, "R") == 3,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- operators
    {
        Machine r = run("int R = 5; void main() { R += 3; R -= 1; R *= 2; R /= 7; R %= 5; }");
        check("compound assignments", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int x = 5; R = x++ + ++x; }");
        check("x=5: x++ + ++x = 5 + 7 = 12", var_of(r.m, r.vars, "R") == 12,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int x = 5; x++; R = x--; }");
        check("x=5: x++ then x-- into R = 6", var_of(r.m, r.vars, "R") == 6,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int x = 3; R = (x = x + 4) * 2; }");
        check("assignment is an expression (14)", var_of(r.m, r.vars, "R") == 14,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- arrays / tables
    {
        Machine r = run("int a[4]; int R; void main() { a[0] = 10; a[1] = 20; a[2] = 30; a[3] = 40;"
                        " R = a[0] + a[1] + a[2] + a[3]; }");
        check("RAM array read/write", var_of(r.m, r.vars, "R") == 100,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int a[5] = {1, 2, 3, 4, 5}; int R; void main() { R = a[4] * 2 + a[0]; }");
        check("RAM array initialiser", var_of(r.m, r.vars, "R") == 11,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int a[6]; int R; void main() { int i; for (i = 0; i < 6; i = i + 1) { a[i] = i * i; }"
                        " R = a[5]; }");
        check("array with loop index", var_of(r.m, r.vars, "R") == 25,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("rom int tbl[3] = {1000, 2000, 3000}; int R; void main() { R = tbl[1] + 1; }");
        check("rom word table", var_of(r.m, r.vars, "R") == 2001,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("rom8 char msg[4] = \"ABC\"; int R; void main() { R = msg[0] + msg[2]; }");
        check("rom8 string bytes", var_of(r.m, r.vars, "R") == (int)'A' + (int)'C',
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int a[3]; int R; void main() { int i = 1; a[i] = 7; i = i + 1; a[i] = 9; R = a[1] + a[2]; }");
        check("array with variable index", var_of(r.m, r.vars, "R") == 16,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    {
        Machine r = run("int a, b = 2, c[2] = {3, 4}; int R;"
                        " void main() { R = a + b + c[0] + c[1]; }");
        check("multiple declarators (a, b = 2, c[2])", var_of(r.m, r.vars, "R") == 9,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int a[] = {1, 2, 3}; int R; void main() { R = a[0] + a[2]; }");
        check("an unsized array takes its size from the initializer",
              var_of(r.m, r.vars, "R") == 4, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int a[] = {1, 2, 3}; int s = 0;"
                        " for (int k = 0, n = 3; k < n; k++) { s += a[k]; } R = s; }");
        check("local array + for (int k = 0, n = N; ...)",
              var_of(r.m, r.vars, "R") == 6, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int a = 1; static int b = 2; bool c = true; int R;"
                        " void main() { R = a + b + c; }");
        check("C qualifiers and bool are accepted",
              var_of(r.m, r.vars, "R") == 4, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- functions
    {
        Machine r = run("int add(int a, int b) { return a + b; }\n"
                        "int R; void main() { R = add(add(1, 2), add(3, 4)); }");
        check("function calls + nesting", var_of(r.m, r.vars, "R") == 10,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int scale(int x, int k = 3) { return x * k; }\n"
                        "int R; void main() { R = scale(5) + scale(5, 2); }");
        check("default arguments (15 + 10 = 25)", var_of(r.m, r.vars, "R") == 25,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int fib(int n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }\n"
                        "int R; void main() { R = fib(10); }");
        check("direct recursion: fib(10) = 55", var_of(r.m, r.vars, "R") == 55,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int fact(int n) { if (n <= 1) { return 1; } return n * fact(n - 1); }\n"
                        "int R; void main() { R = fact(7); }");
        check("recursion: 7! = 5040", var_of(r.m, r.vars, "R") == 5040,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("const int N = 6;\nint a[N]; int R;\n"
                        "void main() { int i; for (i = 0; i < N; i = i + 1) { a[i] = i + 1; } R = a[5]; }");
        check("const used as an array size and a bound", var_of(r.m, r.vars, "R") == 6,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- builtins / library
    {
        Machine r = run("void main() { clear(); pixel(10, 20); render(); }", "cwii");
        int total = 0;
        for (int x = 0; x < 192; x++)
            for (int y = 0; y < 64; y++) total += pix1(r.m, x, y);
        check("pixel() lights exactly one pixel", pix1(r.m, 10, 20) == 1 && total == 1,
              std::to_string(total));
    }
    {
        const std::string src =
            "int R; void main() { memoryWrite(0xD100, 2, 0x1234); "
            "R = memoryRead(0xD100, 2); setStatus(SHIFT, HIGH); "
            "R = R + memoryRead(SB2 + SHIFT - 0xF800, 1); "
            "setStatus(SUN_CHRG, HIGH); "
            "R = R + memoryRead(SB2 + SUN_CHRG - 0xF800, 1); "
            "renderStatus(); setStatus(SHIFT, LOW); setStatus(SUN_CHRG, LOW); }";
        for (const std::string target : {"cw", "cwii"}) {
            Machine r = run(src, target);
            const int offset = target == "cwii" ? 1 : 0;
            const int base = target == "cwii" ? 0xCA54 : 0xDDD3;
            check("memory helpers and status icons work (" + target + ")",
                  var_of(r.m, r.vars, "R") == 0x1236 && r.m.mem[base + offset] == 0 &&
                      r.m.planes[0][offset] == 1 &&
                      r.m.mem[base + 22] == 0 && r.m.planes[0][22] == 1 &&
                      (target != "cwii" || (r.m.planes[1][offset] == 1 && r.m.planes[1][22] == 1)),
                  ex(var_of(r.m, r.vars, "R"), r.m.mem[base + offset], r.m.planes[0][offset]));
        }
    }
    {
        Machine r = run("void main() { clear(); hline(0, 10, 5); render(); }");
        bool ok = true;
        for (int x = 0; x < 10; x++) ok = ok && pix1(r.m, x, 5);
        check("hline() draws a 10-pixel row (x1 exclusive)", ok && !pix1(r.m, 10, 5));
    }
    {
        Machine r = run("void main() { clear(); fillrect(0, 0, 4, 3); render(); }");
        bool ok = true;
        for (int x = 0; x < 4; x++)
            for (int y = 0; y < 3; y++) ok = ok && pix1(r.m, x, y);
        check("fillrect() fills 4x3", ok);
    }
    {
        Machine r = run("void main() { clear(); rect(0, 0, 5, 5); render(); }");
        check("rect() draws a 5x5 outline",
              pix1(r.m, 0, 0) && pix1(r.m, 4, 4) && !pix1(r.m, 2, 2));
    }
    {
        Machine r = run("int R; void main() { clear(); R = text(0, 0, \"Hi\"); }");
        check("text() returns the end x (2 chars -> 12)", var_of(r.m, r.vars, "R") == 12,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { clear(); R = num(0, 0, 12345); }");
        check("num() returns the end x (5 digits -> 30)", var_of(r.m, r.vars, "R") == 30,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { clear(); R = num(0, 0, -32768); }");
        check("num() renders -32768 (6 chars -> 36)", var_of(r.m, r.vars, "R") == 36,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { clear(); print(0, 0, \"n=\", 42); R = 1; render(); }");
        bool any = false;
        for (int x = 0; x < 60; x++)
            for (int y = 0; y < 8; y++) any = any || pix1(r.m, x, y);
        check("print() compiles and runs", var_of(r.m, r.vars, "R") == 1 && any);
    }
    {
        Machine r = run("int R; void main() { clear(); R = text_cw(2, 3, \"A\"); render(); }");
        check("text_cw() draws native 11 px glyphs and returns the end x",
              var_of(r.m, r.vars, "R") == 2 + FONT_CW_W && lit(r.m) == cw_ref(2, 3, "A"),
              ex(var_of(r.m, r.vars, "R"), lit(r.m)));
    }
    {
        Machine r = run("int R; void main() { clear(); R = num_cw(0, 0, -32768); render(); }");
        check("num_cw() renders -32768 at an 11 px advance",
              var_of(r.m, r.vars, "R") == 6 * FONT_CW_W && lit(r.m) == cw_ref(0, 0, "-32768"),
              ex(var_of(r.m, r.vars, "R"), lit(r.m)));
    }
    {
        Machine r = run("int R; void main() { clear(); print_cw(0, 0, \"A\", 42); R = 1; render(); }");
        check("print_cw() lays out the CW Display font",
              var_of(r.m, r.vars, "R") == 1 && lit(r.m) == cw_ref(0, 0, "A42"),
              ex(var_of(r.m, r.vars, "R"), lit(r.m)));
    }
    {
        Machine r = run("int R; void main() { srand(1); R = rand(100); }");
        check("rand() returns a value in range", 0 <= var_of(r.m, r.vars, "R") && var_of(r.m, r.vars, "R") < 100,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = abs(-7) + min(3, 1) + max(2, 9) + clamp(50, 0, 10) + sign(-4); }");
        check("abs/min/max/clamp/sign", var_of(r.m, r.vars, "R") == 7 + 1 + 9 + 10 + (-1),
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- keys
    {
        Machine r = run("int R; void main() { R = key_down(0x20); }", "cwii", 400000,
                        {key_pair(0x20)});
        check("key_down() sees a held key", var_of(r.m, r.vars, "R") == 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = key(); }", "cwii", 400000, {key_pair(0x20)});
        check("key() reports the pressed scan code", var_of(r.m, r.vars, "R") == 0x20,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = K_UP; }");
        int u = uvar_of(r.m, r.vars, "R");
        check("K_* key constants are predefined",
              u == 0x20 || u == 0x28 || u == 0x30 || u == 0x38, std::to_string(u));
    }

    // --------------------------------------------------------------------- both targets
    for (const char* tg : {"cwii", "cw"}) {
        Machine r = run("int R; void main() { int i; for (i = 1; i <= 5; i = i + 1) { R = R + i * i; } }", tg);
        check(std::string(tg) + ": sum of squares 1..5 = 55", var_of(r.m, r.vars, "R") == 55,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------------------------- error cases
    {
        ErrRes e = expect_error("void main() { x = 1; }", "'x' is not defined");
        check("error: use of an undefined variable", e.ok, e.msg);
        e = expect_error("int f(int a, int b) { return a; }\nvoid main() { f(1); }", "takes 2 arguments");
        check("error: too few arguments", e.ok, e.msg);
        e = expect_error("int g() { return 1; }\nvoid main() { g(1); }", "takes 0 arguments");
        check("error: too many arguments", e.ok, e.msg);
        e = expect_error("void main() { break; }", "outside a loop");
        check("error: break outside a loop", e.ok, e.msg);
        e = expect_error("const int C = 1;\nvoid main() { C = 2; }", "cannot assign to constant");
        check("error: assignment to a const", e.ok, e.msg);
        e = expect_error("rom int t[2] = {1, 2};\nvoid main() { t[0] = 5; }", "read-only");
        check("error: write to a rom table", e.ok, e.msg);
        e = expect_error("void main() { int x = 1 }\n", "expected ';'");
        check("error: missing semicolon", e.ok, e.msg);
        e = expect_error("void main() { int x = ; }", "unexpected");
        check("error: missing expression", e.ok, e.msg);
        e = expect_error("void main() { int x = 1; }\nvoid main() { }", "redefinition");
        check("error: duplicate function", e.ok, e.msg);
        e = expect_error("int a[];\nvoid main() { }", "needs a size or an initializer");
        check("error: an unsized array needs an initializer", e.ok, e.msg);
        e = expect_error("void x;\nvoid main() { }", "cannot have type void");
        check("error: a void variable", e.ok, e.msg);
        e = expect_error("void f() { return 1; }\nvoid main() { }", "cannot return a value");
        check("error: a void function returns a value", e.ok, e.msg);
        e = expect_error("int f() { return; }\nvoid main() { }", "must return a value");
        check("error: a non-void function returns nothing", e.ok, e.msg);
    }
    {
        bool hint_ok = false;
        try {
            wiz::build_cwiz("int scores;\nvoid main() { score = 1; }", "cwii");
        } catch (const wiz::CwizError& e) {
            hint_ok = e.code == "sema.undefined-identifier" &&
                      std::string(e.hint).find("did you mean 'scores'") != std::string::npos;
        }
        check("error: undefined name has a code and a suggestion", hint_ok);
    }

    // --------------------------------------------------------------------- pointers
    {
        Machine r = run("int R; void main() { int x = 7; int *p; p = &x; R = *p; *p = 9; R = R + x; }");
        check("int*: &x, *p read and *p write", var_of(r.m, r.vars, "R") == 16,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int a[4] = {10, 20, 30, 40}; void main() { int *p; p = a;"
                        " R = *p + p[1] + *(p + 2) + p[3]; }");
        check("an array decays to a pointer; p[i] and *(p+i)", var_of(r.m, r.vars, "R") == 100,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int a[5] = {1, 2, 3, 4, 5}; void main() { int *p; p = a + 4;"
                        " *p = 55; R = a[4]; }");
        check("pointer arithmetic writes back into the array", var_of(r.m, r.vars, "R") == 55,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int a[5] = {1, 2, 3, 4, 5}; void main() { int *p = a;"
                        " p++; R = *p; p--; R = R * 10 + *p; }");
        check("int* ++ / -- step by one element (2 bytes)", var_of(r.m, r.vars, "R") == 21,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("char buf[6] = \"ABCDE\"; int R; void main() { char *q = buf;"
                        " R = *q + q[2]; q = q + 3; R = R + *q; }");
        check("char* steps by one byte; a RAM char array holds a string",
              var_of(r.m, r.vars, "R") == (int)'A' + (int)'C' + (int)'D',
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int a[4] = {5, 6, 7, 8}; void main() { int *p = &a[1];"
                        " R = *p * 100; p += 2; R = R + *p; }");
        check("&a[i] and p += n", var_of(r.m, r.vars, "R") == 600 + 8,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int a[4] = {5, 6, 7, 8}; void main() { int *p = a; int *q = a + 3;"
                        " R = q - p; R = R * 10 + (p < q) + (p == a); }");
        check("pointer difference and comparison", var_of(r.m, r.vars, "R") == 3 * 10 + 1 + 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int x = 3; int *p = &x; int **pp = &p;"
                        " **pp = 8; R = x; }");
        check("int**: pointer to a pointer", var_of(r.m, r.vars, "R") == 8,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int *gp; int x = 11; void main() { gp = &x; *gp = *gp + 1; }");
        check("a global pointer writes through to a global", var_of(r.m, r.vars, "x") == 12,
              std::to_string(var_of(r.m, r.vars, "x")));
    }
    {
        Machine r = run("int x = 11;\nint *gp = &x;\nvoid main() { *gp = 1; }");
        check("a global pointer can be initialised with &global",
              var_of(r.m, r.vars, "x") == 1, std::to_string(var_of(r.m, r.vars, "x")));
    }
    {
        Machine r = run("int A = 3;\nint R = A + 2;\nvoid main() { }");
        check("a global initialiser may use another global", var_of(r.m, r.vars, "R") == 5,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("void bump(int *p) { *p = *p + 5; }\n"
                        "int R; void main() { int x = 1; bump(&x); R = x; }");
        check("a pointer passed to a function writes back", var_of(r.m, r.vars, "R") == 6,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int sum(int *p, int n) { int i; int t = 0;"
                        " for (i = 0; i < n; i = i + 1) { t = t + p[i]; } return t; }\n"
                        "int R; int a[4] = {2, 4, 6, 8}; void main() { R = sum(a, 4); }");
        check("a pointer walks an array inside a function", var_of(r.m, r.vars, "R") == 20,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int a[3] = {1, 2, 3}; void main() { int *p = a;"
                        " int i; for (i = 0; i < 3; i = i + 1) { *p = *p * 10; p++; }"
                        " R = a[0] + a[1] + a[2]; }");
        check("a pointer walk in a loop rewrites the array", var_of(r.m, r.vars, "R") == 60,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; char buf[4]; void main() { int x = 5; int *p = &x;"
                        " char *q = buf; q[1] = 7; R = buf[1] + (*p); }");
        check("mixed: char* indexing and int* dereference", var_of(r.m, r.vars, "R") == 12,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        ErrRes e = expect_error("void main() { int x = 1; int y = *x; }", "cannot dereference a non-pointer");
        check("error: dereferencing a non-pointer", e.ok, e.msg);
        e = expect_error("const int C = 3;\nvoid main() { int *p = &C; }", "cannot take the address of constant");
        check("error: address of a constant", e.ok, e.msg);
        e = expect_error("rom int t[2] = {1, 2};\nvoid main() { int *p = &t[0]; }",
                         "cannot take the address of ROM table");
        check("error: address of a ROM table", e.ok, e.msg);
        e = expect_error("void main() { int x = 1; x[0] = 2; }", "'x' is not an array or pointer");
        check("error: indexing a non-pointer", e.ok, e.msg);
        e = expect_error("int oops = nope;\nvoid main() { }", "'nope' is not defined");
        check("error: an undefined global initialiser is reported", e.ok, e.msg);
    }
}
