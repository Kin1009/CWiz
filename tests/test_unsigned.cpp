// test_unsigned.cpp - real unsigned 16-bit integers in the CWiz C subset.
//
// This is a CWiz extension: upstream Wiz (the Python-syntax front end) has a
// single signed 16-bit cell and is unchanged.  Everything here is C-subset
// source compiled through wiz::build_cwiz() on "cwii", plus direct ISA checks
// that pin the simulator's already-correct unsigned carry/borrow contract.
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "asm.hpp"
#include "harness.hpp"
#include "lint.hpp"

using namespace wt;

namespace {

Machine run(const std::string& src, const std::string& target = "cwii", long steps = 400000) {
    wiz::BuildResult br = wiz::build_cwiz(src, target);
    wiz::U8 m(br.rom, {}, target);
    m.run(steps);
    return Machine{std::move(m), br.variables, "", br.rom, br.full_asm, br.syms};
}

struct ErrRes {
    bool ok = false;
    std::string msg;
};

ErrRes expect_code(const std::string& src, const std::string& code) {
    try {
        wiz::build_cwiz(src, "cwii");
    } catch (const wiz::CompileError& e) {
        return {e.code == code, std::string(e.what()) + " [" + e.code + "]"};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
    return {false, "no error"};
}

// Hand-assembled ROM helpers (same pattern as tests/test_conformance.cpp).
using Image = std::map<uint32_t, uint8_t>;

int word_at(const Image& img, int at) {
    auto lo = img.find((uint32_t)at), hi = img.find((uint32_t)at + 1);
    return (lo == img.end() ? 0 : lo->second) | ((hi == img.end() ? 0 : hi->second) << 8);
}

wiz::U8 machine_of(const Image& img, int bytes = 64) {
    std::vector<uint8_t> rom;
    for (int i = 0; i < bytes; i++)
        rom.push_back(img.count((uint32_t)i) ? img.at((uint32_t)i) : 0);
    return wiz::U8(rom, {}, "cwii", 0, 0);
}

int er0(const wiz::U8& m) { return m.r[0] | (m.r[1] << 8); }

bool lint_has(const std::string& src, const std::string& rule,
              const std::string& file = "t.cwiz") {
    for (const auto& d : wiz::lint_source(src, file, "cwii"))
        if (d.rule == rule) return true;
    return false;
}

}  // namespace

void wt::run_unsigned() {
    // ------------------------------------------------- literal typing (D2)
    {
        Machine r = run("unsigned int R; void main() { R = 0; }");
        check("unsigned: literal 0", uvar_of(r.m, r.vars, "R") == 0,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 32767; }");
        check("unsigned: literal 32767 stays signed-domain positive",
              uvar_of(r.m, r.vars, "R") == 32767,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 32768; }");
        check("unsigned: unsuffixed 32768 is unsigned",
              uvar_of(r.m, r.vars, "R") == 32768,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 65535; }");
        check("unsigned: unsuffixed 65535 is unsigned 65535",
              uvar_of(r.m, r.vars, "R") == 65535,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 65535U; }");
        check("unsigned: uppercase U suffix forces unsigned",
              uvar_of(r.m, r.vars, "R") == 65535,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 0xFFFFu; }");
        check("unsigned: lowercase u suffix forces unsigned",
              uvar_of(r.m, r.vars, "R") == 65535,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 65535UL; }");
        check("unsigned: lone l/L after u is accepted",
              uvar_of(r.m, r.vars, "R") == 65535,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 65535L; }");
        check("unsigned: lone L suffix is ignored",
              uvar_of(r.m, r.vars, "R") == 65535,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = (0xFFFF == 65535); }");
        check("unsigned: unsuffixed high literals compare as 0..65535",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ---------------------------------------------- arithmetic / wrapping
    {
        Machine r = run("unsigned int R; void main() { R = 65535U + 1; }");
        check("unsigned: 65535U + 1 wraps to 0", uvar_of(r.m, r.vars, "R") == 0,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 40000U / 3; }");
        check("unsigned: 40000U / 3 == 13333", uvar_of(r.m, r.vars, "R") == 13333,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 40000U % 7; }");
        check("unsigned: 40000U % 7 == 2", uvar_of(r.m, r.vars, "R") == 2,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; void main() { R = 0x8000U >> 1; }");
        check("unsigned: 0x8000U >> 1 == 16384 (logical)",
              uvar_of(r.m, r.vars, "R") == 16384,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; unsigned int a = 0x8000; void main() { R = a >> 1; }");
        check("unsigned: variable 0x8000 >> 1 == 16384 (logical)",
              uvar_of(r.m, r.vars, "R") == 16384,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }

    // --------------------------------------- comparisons and conversions
    {
        Machine r = run("int R; void main() { R = -1 > 1U; }");
        check("unsigned: -1 > 1U is true (unsigned wins)",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; int s = -1; unsigned int u = 1; void main() { R = (s < u); }");
        check("unsigned: mixed signed/unsigned converts -1 to 65535 (s < u is false)",
              var_of(r.m, r.vars, "R") == 0, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; unsigned int a = 40000; unsigned int b = 1000;"
                        " void main() { R = a > b; }");
        check("unsigned: > compares by magnitude, not sign",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; unsigned int a = 1; unsigned int b = 60000;"
                        " void main() { R = a < b; }");
        check("unsigned: < compares by magnitude, not sign",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = ((unsigned int)-1 > 100); }");
        check("unsigned: cast to unsigned changes a comparison",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = ((int)65535u == -1); }");
        check("unsigned: cast back to int preserves the bits (65535u -> -1)",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------- sizeof, arrays, aggregates
    {
        Machine r = run("int R; void main() { R = sizeof(unsigned int); }");
        check("unsigned: sizeof(unsigned int) == 2", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = sizeof(unsigned char); }");
        check("unsigned: sizeof(unsigned char) == 1", var_of(r.m, r.vars, "R") == 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; unsigned int a[3]; void main() {"
                        " a[0] = 65535; R = a[0] > 100; }");
        check("unsigned: array element keeps unsigned semantics",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("unsigned int R; unsigned int a[2]; void main() { a[1] = 65535; R = a[1]; }");
        check("unsigned: array element stores the full 0..65535 range",
              uvar_of(r.m, r.vars, "R") == 65535,
              std::to_string(uvar_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("struct S { unsigned int u; int s; }; unsigned int R;"
                        " void main() { struct S p; p.u = 65535; p.s = 1; R = p.u > p.s; }");
        check("unsigned: struct field keeps unsigned semantics",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        // The compiler core currently restricts ROM tables to `int` elements;
        // casting the loaded word gives the unsigned 0..65535 view. (Native
        // `rom unsigned int` element typing is a core-side follow-up.)
        Machine r = run("rom int t[2] = {0xFFFF, 1000}; unsigned int R;"
                        " void main() { R = (unsigned int)t[0] > t[1]; }");
        check("unsigned: rom word table read through an unsigned cast",
              var_of(r.m, r.vars, "R") == 1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { unsigned char c = 255; c = c + 1; R = c; }");
        check("unsigned: 8-bit unsigned char wraps 255+1 to 0",
              var_of(r.m, r.vars, "R") == 0, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // --------------------------------------------------- signed regressions
    {
        Machine r = run("int R; int a = -2; void main() { R = a >> 1; }");
        check("unsigned: signed >> stays arithmetic (-2 >> 1 == -1)",
              var_of(r.m, r.vars, "R") == -1, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = -7 / 2; }");
        check("unsigned: signed / still truncates toward zero (-7 / 2 == -3)",
              var_of(r.m, r.vars, "R") == -3, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = -7 % 3; }");
        check("unsigned: signed % still takes the dividend's sign (-7 % 3 == -1)",
              var_of(r.m, r.vars, "R") == -1, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ----------------------------------------------------------- diagnostics
    {
        ErrRes e = expect_code("unsigned int x = 70000; void main() { }", "lex.literal-out-of-range");
        ErrRes f = expect_code("unsigned int x = 0x10000; void main() { }",
                               "lex.literal-out-of-range");
        check("unsigned: > 0xFFFF (decimal and hex) is lex.literal-out-of-range, not wrap",
              e.ok && f.ok, e.msg + " | " + f.msg);
    }

    // ------------------------------------------------------------ codegen
    {
        wiz::BuildResult br = wiz::build_cwiz(
            "unsigned int R; void main() { unsigned int a = 60000;"
            " R = a / 3 + (a >> 1); }", "cwii");
        const bool udiv = br.full_asm.find("BL __udiv16") != std::string::npos;
        const bool shr = br.full_asm.find("BL __shr16") != std::string::npos;
        const bool signed_helpers = br.full_asm.find("BL __div16") != std::string::npos ||
                                    br.full_asm.find("BL __sra16") != std::string::npos;
        check("unsigned: / and >> use __udiv16/__shr16, never signed helpers",
              udiv && shr && !signed_helpers);
    }

    // ------------------------------------------- assembler / simulator locks
    {
        wiz::Assembler a("nX-U16/100");
        auto img = a.assemble("ORG 0\nJLT 0\nJGE 0\nJGT 0\nJLE 0\nJLTS 0\nJGES 0\n");
        // Each J<cond> is a 3-word (6-byte) encoding.
        check("sim/unsigned: JLT/JGE/JGT/JLE use carry conditions; signed S forms distinct",
              word_at(img, 0) == 0xC002 && word_at(img, 6) == 0xC102 &&
              word_at(img, 12) == 0xC302 && word_at(img, 18) == 0xC202 &&
              word_at(img, 24) == 0xC402 && word_at(img, 30) == 0xC502,
              wiz::fmt("%04X %04X %04X %04X %04X %04X", word_at(img, 0), word_at(img, 6),
                       word_at(img, 12), word_at(img, 18), word_at(img, 24), word_at(img, 30)));
    }
    {
        // 1000 < 60000 needs the CMPC borrow: low byte alone would say "not <".
        wiz::Assembler a("nX-U16/100");
        auto img = a.assemble(
            "ORG 0\n"
            "MOV R0, #0xE8\nMOV R1, #0x03\n"   // 1000
            "MOV R2, #0x60\nMOV R3, #0xEA\n"   // 60000
            "CMP R0, R2\nCMPC R1, R3\n"
            "JLT L_ok\nMOV R4, #0\nB L_done\nL_ok:\nMOV R4, #1\nL_done:\nB L_done\n");
        wiz::U8 m = machine_of(img);
        m.run(200);
        check("sim/unsigned: CMP+CMPC borrow selects unsigned JLT (1000 < 60000)",
              m.r[4] == 1 && m.C == 1, wiz::fmt("r4=%d C=%d", m.r[4], m.C));
    }
    {
        // The reverse comparison clears the borrow => unsigned JGE is taken.
        wiz::Assembler a("nX-U16/100");
        auto img = a.assemble(
            "ORG 0\n"
            "MOV R0, #0x40\nMOV R1, #0x9C\n"   // 40000
            "MOV R2, #0xE8\nMOV R3, #0x03\n"   // 1000
            "CMP R0, R2\nCMPC R1, R3\n"
            "JGE L_ok\nMOV R4, #0\nB L_done\nL_ok:\nMOV R4, #1\nL_done:\nB L_done\n");
        wiz::U8 m = machine_of(img);
        m.run(200);
        check("sim/unsigned: carry clear selects unsigned JGE (40000 >= 1000)",
              m.r[4] == 1 && m.C == 0, wiz::fmt("r4=%d C=%d", m.r[4], m.C));
    }
    {
        // Hardware DIV is unsigned and must stay that way.
        wiz::Assembler a("nX-U16/100");
        auto img = a.assemble(
            "ORG 0\nMOV R0, #0xFE\nMOV R1, #0xFF\nMOV R2, #3\nDIV ER0, R2\nB L_done\n"
            "L_done:\nB L_done\n");
        wiz::U8 m = machine_of(img);
        m.run(200);
        check("sim/unsigned: DIV ERn,Rm is unsigned (0xFFFE / 3 == 0x5554, rem 2)",
              er0(m) == 0x5554 && m.r[2] == 2,
              wiz::fmt("ER0=%04X r2=%d", er0(m), m.r[2]));
    }
    {
        // Signed-only ISA behavior is unchanged by the extension.
        wiz::Assembler a("nX-U16/100");
        auto e = a.assemble("ORG 0\nMOV R0, #0xFE\nEXTBW ER0\nB L_done\nL_done:\nB L_done\n");
        wiz::U8 me = machine_of(e);
        me.run(200);
        wiz::Assembler b("nX-U16/100");
        // -64 is encoded as 0x40 in the sign-extending 7-bit immediate form.
        auto v = b.assemble("ORG 0\nMOV ER0, #-64\nB L_done\nL_done:\nB L_done\n");
        wiz::U8 mv = machine_of(v);
        mv.run(200);
        check("sim/unsigned: EXTBW still sign-extends and 7-bit MOV ERn still sign-extends",
              er0(me) == 0xFFFE && er0(mv) == 0xFFC0,
              wiz::fmt("EXTBW=%04X MOV=%04X", er0(me), er0(mv)));
    }

    // --------------------------------------------------------- lint rules
    {
        const char* src = "unsigned int u = -1;\n"
                          "int s = 1;\n"
                          "int main() { int v = s + 0xFFFF; return u < 0; }\n";
        check("lint/unsigned: negative initializer is advisory",
              lint_has(src, "type/unsigned-negative-initializer"));
        check("lint/unsigned: unsigned < 0 is advisory",
              lint_has(src, "type/unsigned-compare-zero"));
        check("lint/unsigned: implicit unsigned literal in signed expression is a note",
              lint_has(src, "type/unsigned-literal-implicit"));
        check("lint/unsigned: code field is attached to a curated type rule", [src] {
            for (const auto& d : wiz::lint_source(src, "t.cwiz", "cwii"))
                if (d.rule == "type/unsigned-negative-initializer")
                    return d.code == "type.negative-to-unsigned";
            return false;
        }());
        check("lint/unsigned: type/* rules never reject a program", [src] {
            for (const auto& d : wiz::lint_source(src, "t.cwiz", "cwii"))
                if (d.rule.rfind("type/", 0) == 0 && d.severity == wiz::LintSeverity::Error)
                    return false;
            return true;
        }());
        // Wiz (Python syntax) never gets the CWiz-only type rules.
        for (const char* rule : {"type/unsigned-negative-initializer",
                                 "type/unsigned-compare-zero", "type/signed-unsigned-mix",
                                 "type/unsigned-literal-implicit", "type/unsigned-right-shift"})
            check(std::string("lint/unsigned: cwiz-only gate for ") + rule,
                  !lint_has(src, rule, "t.wz"));
    }
    {
        check("lint/unsigned: explicit u suffix is not flagged",
              !lint_has("int main() { int s = 1; return s + 0xFFFFu; }\n",
                        "type/unsigned-literal-implicit"));
        check("lint/unsigned: > 0xFFFF is a compiler error, not a lint note",
              !lint_has("unsigned int x = 65536; void main() { }\n",
                        "type/unsigned-literal-implicit"));
        check("lint/unsigned: right shift of unsigned is a note",
              lint_has("unsigned int u = 1; int main() { return u >> 1; }\n",
                       "type/unsigned-right-shift"));
        check("lint/unsigned: mixed operands warn",
              lint_has("int s = 1;\nunsigned int u = 2;\nint main() { return s < u; }\n",
                       "type/signed-unsigned-mix"));
        check("lint/unsigned: unary minus on unsigned warns",
              lint_has("unsigned int u = 1; int main() { return -u; }\n",
                       "type/unsigned-unary-minus"));
        check("lint/unsigned: conflicting signedness qualifiers warn",
              lint_has("unsigned signed int u = 1; int main() { return u; }\n",
                       "type/unsigned-qualifier-conflict"));
    }
}
