// test_conformance.cpp - C11/C17/C23 conformance rows and directive grammar.
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "build.hpp"
#include "cpre.hpp"
#include "cwiz.hpp"
#include "directives.hpp"
#include "harness.hpp"
#include "rop.hpp"

using namespace wt;

namespace {

Machine run(const std::string& src, const std::string& target = "cwii") {
    wiz::BuildResult br = wiz::build_cwiz(src, target);
    wiz::U8 m(br.rom, {}, target);
    m.run(500000);
    return Machine{std::move(m), br.variables, "", br.rom, br.full_asm, br.syms};
}

struct Err {
    bool ok = false;
    std::string msg;
    std::string code;
    int line = 0;
};

Err err_of(const std::string& src, const std::string& target = "cwii") {
    try {
        wiz::build_cwiz(src, target);
    } catch (const wiz::CompileError& e) {
        return {true, e.what(), e.code, e.line};
    } catch (const std::exception& e) {
        return {true, e.what(), "", 0};
    }
    return {false, "no error", "", 0};
}

std::string tmp_path(const std::string& name) {
    namespace fs = std::filesystem;
    fs::path p = fs::path(wt::root_path("temp/conformance")) / name;
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    return p.string();
}

}  // namespace

void wt::run_conformance() {
    // ------------------------------------------------------------ C23 literals
    {
        Machine r = run("int R; void main() { R = 0b1010 + 1'000; }");
        check("C23: binary literals and digit separators", var_of(r.m, r.vars, "R") == 1010,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { char8_t c = u8'A'; R = c; }");
        check("C23: u8 character literal and char8_t", var_of(r.m, r.vars, "R") == 65,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = sizeof(u8\"abc\"); }");
        check("C23: u8 string literal is a byte string", var_of(r.m, r.vars, "R") == 4,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { int *p = nullptr; R = (p == 0); }");
        check("C23: nullptr is a null pointer constant", var_of(r.m, r.vars, "R") == 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ static_assert
    {
        Machine r = run("static_assert(1 + 1 == 2); int R; void main() {"
                        " static_assert(sizeof(int) == 2, \"16-bit int\"); R = 1; }");
        check("C11/C23: static_assert one and two argument forms",
              var_of(r.m, r.vars, "R") == 1);
    }
    {
        Err e = err_of("static_assert(0, \"nope\"); void main() { }");
        check("error: static_assert failure names its message",
              e.ok && e.code == "sema.static-assert" &&
                  e.msg.find("nope") != std::string::npos,
              e.msg);
    }

    // ------------------------------------------------------------ alignof/alignas
    {
        Machine r = run("int R; void main() {"
                        " R = alignof(int) + alignof(char) * 10 + _Alignof(int) * 100; }");
        check("C11/C23: alignof type forms", var_of(r.m, r.vars, "R") == 212,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; alignas(4) int x; void main() {"
                        " R = ((int)&x) % 4; }");
        check("C11/C23: alignas aligns a global", var_of(r.m, r.vars, "R") == 0,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ _Generic
    {
        Machine r = run("int R; void main() { int x = 3;"
                        " R = _Generic(x, char: 1, int: 2, default: 9); }");
        check("C11: _Generic selects int", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { char c = 3;"
                        " R = _Generic(c, char: 1, int: 2, default: 9); }");
        check("C11: _Generic selects char", var_of(r.m, r.vars, "R") == 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { char *p = 0;"
                        " R = _Generic(p, int*: 1, default: 7); }");
        check("C11: _Generic falls back to default", var_of(r.m, r.vars, "R") == 7,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ C23 type syntax
    {
        Machine r = run("constexpr int N = 3; int R; void main() {"
                        " constexpr int K = 2; R = N * 10 + K; }");
        check("C23: constexpr file/local objects", var_of(r.m, r.vars, "R") == 32,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { auto x = 7; typeof(int) y = 2;"
                        " R = x * 10 + y; }");
        check("C23: auto and typeof(int)", var_of(r.m, r.vars, "R") == 72,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("enum E : unsigned char { A = 4 }; int R;"
                        " void main() { R = A; }");
        check("C23: fixed-underlying enum syntax", var_of(r.m, r.vars, "R") == 4,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { _BitInt(8) a = 3; unsigned _BitInt(16) b = 4;"
                        " R = a + b; }");
        check("C23: _BitInt(8/16) maps to the 8/16-bit model",
              var_of(r.m, r.vars, "R") == 7, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Err e = err_of("_BitInt(32) x; void main() { }");
        check("error: _BitInt(32) is a proven target limitation",
              e.ok && e.code == "sema.bitint-width", e.msg);
    }
    {
        Machine r = run("int R; void main() { [[maybe_unused]] int x = 1;"
                        " switch (x) { case 1: [[fallthrough]]; default: R = 5; } }");
        check("C23: [[attribute]] syntax is accepted", var_of(r.m, r.vars, "R") == 5,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("_Thread_local int t; _Atomic int a; int R;"
                        " void main() { t = 1; a = 2; R = t + a; }");
        check("C11: _Thread_local/_Atomic parse on the freestanding target",
              var_of(r.m, r.vars, "R") == 3, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("inline int f(void) { return 3; } _Noreturn void stop(void) { }"
                        " int R; void main() { R = f(); }");
        check("C11: inline/_Noreturn/restrict spellings",
              var_of(r.m, r.vars, "R") == 3, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ preprocessor
    {
        Machine r = run("#define HAVE 1\n#if 0\nint a;\n#elifdef HAVE\nint R;\n#else\n"
                        "int R;\n#endif\nvoid main() { R = 1; }");
        check("C23: #elifdef", var_of(r.m, r.vars, "R") == 1);
        Machine q = run("#define HAVE 1\n#if 0\nint a;\n#elifndef HAVE\nint R;\n#else\n"
                        "int R;\n#endif\nvoid main() { R = 2; }");
        check("C23: #elifndef", var_of(q.m, q.vars, "R") == 2);
    }
    {
        std::string p = tmp_path("has.cwiz");
        std::ofstream(p) << "int included;\n";
        Machine r = run("#if __has_include(\"" + p + "\")\nint R;\nvoid main(){R=1;}\n"
                        "#else\nint R;\nvoid main(){R=0;}\n#endif\n");
        check("C23: __has_include(quoted)", var_of(r.m, r.vars, "R") == 1,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("#if __has_c_attribute(nodiscard)\nint R;\nvoid main(){R=1;}\n"
                        "#else\nint R;\nvoid main(){R=0;}\n#endif\n");
        check("C23: __has_c_attribute(nodiscard)", var_of(r.m, r.vars, "R") == 1);
    }
    {
        std::string p = tmp_path("embed.bin");
        std::ofstream(p, std::ios::binary) << "\x01\x02\xff";
        std::string text = wiz::preprocess_cwiz("#embed \"" + p + "\"\n", "t.cwiz", "cwii");
        check("C23: #embed expands bytes", text.find("1, 2, 255") != std::string::npos, text);
    }
    {
        // #line changes the diagnostics mapping.
        Err e = err_of("void f() {\n#line 100 \"virtual.cwiz\"\n  x = 1;\n}\n");
        wiz::CwizPos p = wiz::cwiz_error_pos("t.cwiz", e.line);
        check("C23: #line remaps diagnostics", e.ok && p.line == 100, e.msg + " line=" +
                                                                       std::to_string(p.line));
    }
    {
        // Stringizing, pasting and __VA_OPT__ are expanded before lexing.
        Machine r = run("#define STR(x) #x\n#define CAT(a,b) a##b\n"
                        "#define OPT(a, ...) a __VA_OPT__(+ 2)\n"
                        "int CAT(R, 1);\nint R; void main() {"
                        " R = OPT(3) + sizeof(STR(hi)) - 1; }");
        (void)r;
        check("C23: macro stringizing/pasting/__VA_OPT__ compile", true);
    }

    // ------------------------------------------------------------ aggregates
    {
        Machine r = run("struct S { int n; char data[]; }; int R; void main() {"
                        " R = sizeof(struct S); }");
        check("C99: flexible array member contributes no size", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("struct S { int a; struct { int b; char c; }; };"
                        " int R; void main() { struct S s; s.a = 1; s.b = 2; s.c = 3;"
                        " R = s.a * 100 + s.b * 10 + s.c; }");
        check("C11: anonymous struct member", var_of(r.m, r.vars, "R") == 123,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("struct S { int a; union { int b; char c; }; };"
                        " int R; void main() { struct S s; s.b = 0x0102; R = s.c; }");
        check("C11: anonymous union member", var_of(r.m, r.vars, "R") == 2,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("struct S { int x : 3; int y; }; int R; void main() {"
                        " R = sizeof(struct S); }");
        check("C89: bitfield declaration is syntax-only", var_of(r.m, r.vars, "R") == 4,
              std::to_string(var_of(r.m, r.vars, "R")));
        Err e = err_of("struct S { int x : 3; }; void main() { struct S s;"
                       " s.x = 1; }");
        check("error: bitfield access is a documented syntax-only gap",
              e.ok && e.code == "sema.bitfield-access", e.msg);
    }

    // ------------------------------------------------------------ initializers
    {
        Machine r = run("struct P { int x; int y; char c; }; int R; void main() {"
                        " struct P p = { .y = 5, .c = 2, .x = 1 };"
                        " R = p.x * 100 + p.y * 10 + p.c; }");
        check("C99: designated struct initializer", var_of(r.m, r.vars, "R") == 152,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int a[6] = { [4] = 7, 8 }; int R; void main() {"
                        " R = a[0] + a[4] * 10 + a[5] * 100; }");
        check("C99: designated array initializer", var_of(r.m, r.vars, "R") == 870,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("struct P { int x; int y; }; int R; void main() {"
                        " R = ((struct P){ .x = 3, .y = 4 }).x * 10"
                        " + ((struct P){ 1, 2 }).y; }");
        check("C99: struct compound literal", var_of(r.m, r.vars, "R") == 32,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("int R; void main() { R = (int){ 9 }; }");
        check("C99: scalar compound literal", var_of(r.m, r.vars, "R") == 9,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ freestanding headers
    {
        Machine r = run("#include <stdint.h>\n#include <limits.h>\n#include <stddef.h>\n"
                        "#include <stdalign.h>\n#include <stdnoreturn.h>\n"
                        "int R; void main() { int16_t a = INT16_MAX; uint8_t b = UINT8_MAX;"
                        " size_t n = sizeof(int); R = (a == 32767) + (b == 255) * 10"
                        " + (n == 2) * 100 + (alignof(int) == 2) * 1000; }");
        check("freestanding headers: stdint/limits/stddef/stdalign/stdnoreturn",
              var_of(r.m, r.vars, "R") == 1111, std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("#include <string.h>\nchar a[8]; char b[8]; int R;"
                        " void main() { strcpy(a, \"abc\"); memset(b, 0, 8);"
                        " memcpy(b, a, 4); strcat(b, \"d\");"
                        " R = strlen(b) * 100 + (strcmp(a, \"abc\") == 0) * 10"
                        " + (memcmp(a, \"abc\", 3) == 0); }");
        check("freestanding <string.h>: memcpy/memset/strlen/strcmp/strcat",
              var_of(r.m, r.vars, "R") == 411,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ Wiz import
    {
        namespace fs = std::filesystem;
        std::string dir = wt::root_path("temp/import");
        std::error_code ec;
        fs::create_directories(dir, ec);
        wiz::write_text_file(dir + "/lib.wz",
                             "VALUE = 3\ndef add(n):\n    return n + VALUE\n");
        std::string main_src = "import lib\nR = 0\ndef main():\n    global R\n"
                               "    R = add(4)\n";
        wiz::write_text_file(dir + "/main.wz", main_src);
        wiz::BuildResult br = wiz::build_source(main_src, "cwii", dir + "/main.wz");
        wiz::U8 m(br.rom, {}, "cwii");
        m.run(100000);
        check("Wiz: import inlines a sibling .wz file",
              var_of(m, br.variables, "R") == 7,
              std::to_string(var_of(m, br.variables, "R")));
        try {
            wiz::build_source("import missing\nR = 0\n", "cwii", dir + "/main.wz");
            check("Wiz: missing import is diagnosed", false, "no error");
        } catch (const wiz::CompileError& e) {
            check("Wiz: missing import is diagnosed", e.code == "wiz.import-not-found", e.what());
        }
    }

    // ------------------------------------------------------------ function pointers
    {
        Machine r = run("int inc(int x) { return x + 1; }"
                        " int (*fp)(int); int R; void main() { fp = inc; R = fp(4); }");
        check("C: function pointer call through a variable", var_of(r.m, r.vars, "R") == 5,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Machine r = run("typedef int (*FN)(int); int add2(int x) { return x + 2; }"
                        " FN g; int R; void main() { g = add2; R = g(5); }");
        check("C: typedef function pointer", var_of(r.m, r.vars, "R") == 7,
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        wiz::Assembler a("nX-U16/100");
        auto img = a.assemble("ORG 0\nB ER2\nBL ER2\n");
        int w0 = (img.count(0) ? img[0] : 0) | ((img.count(1) ? img[1] : 0) << 8);
        int w1 = (img.count(2) ? img[2] : 0) | ((img.count(3) ? img[3] : 0) << 8);
        check("assembler: B ERn / BL ERn put n in the register field",
              w0 == 0xF022 && w1 == 0xF023,
              wiz::fmt("%04X %04X", w0, w1));
    }

    {
        wiz::Assembler a("nX-U16/100");
        auto img = a.assemble("ORG 0\nPUSH PSW\nPOP PSW\nB 4\n");
        std::vector<uint8_t> rom;
        for (int i = 0; i < 16; i++) rom.push_back(img.count((uint32_t)i) ? img[(uint32_t)i] : 0);
        wiz::U8 m(rom, {}, "cwii", 0, 0);
        std::string why = m.run(1000);
        check("sim: PUSH/POP PSW is available and balanced", why.find("halt") != std::string::npos,
              why);
        auto pc = a.assemble("ORG 0\nPUSH PC\nPOP PC\n");
        int push_pc = (pc.count(0) ? pc[0] : 0) | ((pc.count(1) ? pc[1] : 0) << 8);
        int pop_pc = (pc.count(2) ? pc[2] : 0) | ((pc.count(3) ? pc[3] : 0) << 8);
        check("assembler: PUSH PC / POP PC encode their bits",
              push_pc == 0xF2CE && pop_pc == 0xF28E,
              wiz::fmt("%04X %04X", push_pc, pop_pc));
        bool bad_reg = false;
        try {
            wiz::Assembler b("nX-U16/100");
            b.assemble("ORG 0\nPUSH R20\n");
        } catch (const std::exception&) {
            bad_reg = true;
        }
        check("assembler: register operands are bounds-checked", bad_reg);
    }

    // ------------------------------------------------------------ varargs
    {
        Machine r = run("#include <stdarg.h>\n"
                        "int sum(int n, ...) { va_list ap; int t = 0;"
                        " va_start(ap, n); while (n > 0) { t = t + va_arg(ap, int);"
                        " n = n - 1; } va_end(ap); return t; }"
                        " int R; void main() { R = sum(3, 1, 2, 3); }");
        check("C89: variadic function with va_start/va_arg", var_of(r.m, r.vars, "R") == 6,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ freestanding extras
    {
        Machine r = run("#include <stdckdint.h>\n#include <string.h>\n"
                        "int R; void main() { int v = 0; int ov = ckd_add(&v, 32767, 1);"
                        " char *d = strdup(\"xy\");"
                        " R = (ov != 0) * 100 + v + strlen(d); }");
        check("C23: ckd_add/strdup freestanding subset",
              var_of(r.m, r.vars, "R") == -32666,
              std::to_string(var_of(r.m, r.vars, "R")));
    }

    {
        Machine r = run("rom int t[2] = {1 << 16, 1 << 20}; int R;"
                        " void main() { R = t[0] + t[1]; }");
        check("constant folding: out-of-range shifts agree everywhere",
              var_of(r.m, r.vars, "R") == 0, std::to_string(var_of(r.m, r.vars, "R")));
    }

    {
        Err e = err_of("#binary \"x.bin\"\nvoid main() { }\n", "cwii");
        check("error: #binary on a non-fwnx target is rejected",
              e.ok && e.code == "build.binary-target", e.msg);
        std::string dir = wt::root_path("temp/binary");
        std::filesystem::create_directories(dir);
        wiz::write_binary_file(dir + "/fw.bin", {0x12, 0x34, 0x56, 0x78});
        wiz::BuildResult br =
            wiz::build_cwiz("#binary \"fw.bin\"\n", "fwnx", dir + "/t.cwiz");
        check("C: #binary resolves against the source file", br.rom.size() == 4 &&
                  br.rom[0] == 0x12 && br.rom[3] == 0x78);
    }

    // ------------------------------------------------------------ target limits
    {
        Err e = err_of("void main() { int x = 1.5; }");
        check("experiment: floating literals are rejected on this target",
              e.ok && e.code == "lex.float", e.msg);
        Machine r = run("int R; void main() { R = sizeof(long); }");
        check("experiment: sizeof(long) is 2 (no 32-bit model)",
              var_of(r.m, r.vars, "R") == 2, std::to_string(var_of(r.m, r.vars, "R")));
    }

    // ------------------------------------------------------------ custom types
    {
        Machine r = run("rom int t[2] = {3, 4}; rom8 char s[] = \"hi\"; int R;"
                        " void main() { R = t[1] + _rpeek(s); }");
        check("types: canonical rom/rom8 placement", var_of(r.m, r.vars, "R") == 4 + 'h',
              std::to_string(var_of(r.m, r.vars, "R")));
    }
    {
        Err e = err_of("rom char t[2]; void main() { }");
        check("error: rom element type must be int",
              e.ok && e.code == "sema.type-placement", e.msg);
        e = err_of("rom8 int t[2]; void main() { }");
        check("error: rom8 element type must be char",
              e.ok && e.code == "sema.type-placement", e.msg);
        e = err_of("rom int x; void main() { }");
        check("error: rom requires an array", e.ok && e.code == "sema.type-placement", e.msg);
        e = err_of("__normalized__ int x[2] = \"a\"; void main() { }");
        check("error: __normalized__ placement", e.ok &&
                  e.code == "preproc.attribute-placement", e.msg);
    }

    // ------------------------------------------------------------ example migration
    {
        namespace fs = std::filesystem;
        std::string bad;
        int files = 0;
        std::error_code ec;
        fs::path root = wt::root_path("examples");
        if (fs::exists(root, ec)) {
            for (auto it = fs::recursive_directory_iterator(root, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file()) continue;
                std::string ext = it->path().extension().string();
                if (ext != ".cwiz" && ext != ".wz" && ext != ".md") continue;
                files++;
                std::ifstream in(it->path());
                std::string line;
                int ln = 0;
                while (std::getline(in, line)) {
                    ln++;
                    std::string t = line;
                    size_t a = t.find_first_not_of(" \t");
                    t = a == std::string::npos ? "" : t.substr(a);
                    auto flag = [&](const std::string& why) {
                        if (bad.empty())
                            bad = it->path().string() + ":" + std::to_string(ln) + ": " + why;
                    };
                    if (t.rfind("#org ", 0) == 0) {
                        std::string v = t.substr(5);
                        size_t sp = v.find_first_of(" \t");
                        if (sp != std::string::npos) v = v.substr(0, sp);
                        if (!(v.rfind("0x", 0) == 0 || v.rfind("0X", 0) == 0)) flag("bare-hex #org");
                    } else if (t.rfind("#for ", 0) == 0) {
                        std::string v = t.substr(5);
                        std::string first = v.substr(0, v.find(','));
                        std::string canon;
                        if (wiz::canonical_target_name(first, canon) && canon != first)
                            flag("#for alias");
                    } else if (t.rfind("#launcher ", 0) == 0) {
                        if (t.find("shared") != std::string::npos ||
                            t.find("chung") != std::string::npos ||
                            t.find("stub") != std::string::npos)
                            flag("#launcher alias");
                    } else if (t.rfind("#pad ", 0) == 0 && t.find("0x") != std::string::npos) {
                        flag("hex #pad");
                    } else if (t.rfind("#slot ", 0) == 0 && t.find("0x") != std::string::npos) {
                        flag("hex #slot");
                    } else if (t.rfind("#include <", 0) == 0) {
                        std::string h = t.substr(10);
                        size_t e = h.find('>');
                        h = h.substr(0, e);
                        if (h != "stdint.h" && h != "stdbool.h" && h != "stddef.h" &&
                            h != "limits.h" && h != "string.h" && h != "stdalign.h" &&
                            h != "stdnoreturn.h")
                            flag("non-freestanding angle include");
                    }
                    const bool comment = (t.rfind("//", 0) == 0 || t.rfind("/*", 0) == 0);
                    if (ext == ".cwiz" && !comment) {
                        size_t r8 = t.find("rom8");
                        if (r8 != std::string::npos && r8 + 4 < t.size() &&
                            t.compare(r8 + 4, 5, " char") != 0)
                            flag("rom8 without char");
                        size_t r = t.find("rom");
                        if (r8 == std::string::npos && r != std::string::npos &&
                            r + 3 < t.size() && t.compare(r + 3, 4, " int") != 0)
                            flag("rom without int");
                        size_t n = t.find("__normalized__");
                        if (n != std::string::npos) {
                            std::string after = t.substr(n + 14);
                            if (n != 0 || (after.rfind(" rom8 char ", 0) != 0 &&
                                           after.rfind(" char ", 0) != 0))
                                flag("__normalized__ placement");
                        }
                    }
                }
            }
        }
        check("examples: canonical directive/type forms", bad.empty(),
              bad.empty() ? std::to_string(files) + " files" : bad);
    }
    {
        namespace fs = std::filesystem;
        std::string bad;
        int built = 0;
        std::error_code ec;
        fs::path root = wt::root_path("examples");
        if (fs::exists(root, ec)) {
            for (auto it = fs::recursive_directory_iterator(root, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file()) continue;
                std::string ext = it->path().extension().string();
                if (ext != ".cwiz" && ext != ".wz") continue;
                std::string p = it->path().string();
                bool ok = false;
                try {
                    std::string text;
                    std::ifstream in(it->path());
                    std::stringstream ss;
                    ss << in.rdbuf();
                    text = ss.str();
                    std::string target = p.find("580") != std::string::npos
                                             ? "580"
                                             : (p.find("880") != std::string::npos ? "880" : "cwii");
                    if (text.find("#cwapp") != std::string::npos ||
                        text.find("for cwapp") != std::string::npos) target = "cwapp";
                    if (text.find("#rop") != std::string::npos) {
                        wiz::RopResult r = wiz::build_rop(text, target == "880" ? "880" : "580",
                                                          ext == ".cwiz" ? "cwiz" : "wiz");
                        ok = r.ok;
                    } else if (ext == ".cwiz") {
                        wiz::build_cwiz(text, target, p);
                        ok = true;
                    } else {
                        wiz::build_source(text, target);
                        ok = true;
                    }
                } catch (...) {
                    ok = false;
                }
                if (ok) built++;
                else if (bad.empty()) bad = p;
            }
        }
        check("examples: every source compiles", bad.empty(),
              bad.empty() ? std::to_string(built) + " programs" : ("failed: " + bad));
    }

    {
        namespace fs = std::filesystem;
        std::string bad;
        int ran = 0, skipped = 0;
        std::error_code ec;
        fs::path root = wt::root_path("examples");
        if (fs::exists(root, ec)) {
            for (auto it = fs::recursive_directory_iterator(root, ec);
                 !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file()) continue;
                std::string ext = it->path().extension().string();
                if (ext != ".cwiz" && ext != ".wz") continue;
                std::string p = it->path().string();
                std::stringstream ss;
                { std::ifstream in(it->path()); ss << in.rdbuf(); }
                std::string text = ss.str();
                if (text.find("#rop") != std::string::npos) { skipped++; continue; }
                bool cwapp = text.find("#cwapp") != std::string::npos ||
                             text.find("for cwapp") != std::string::npos;
                std::string target = cwapp ? "cwapp"
                                     : (p.find("580") != std::string::npos ? "cw"
                                        : (p.find("880") != std::string::npos ? "cwii"
                                                                              : "cwii"));
                try {
                    wiz::BuildResult br = ext == ".cwiz"
                                              ? wiz::build_cwiz(text, target, p)
                                              : wiz::build_source(text, target, p);
                    wiz::U8 m = cwapp ? wiz::U8(br.rom, {}, "cwapp",
                                                (br.syms.at("_APP") >> 16) & 0xFFFF,
                                                br.syms.at("_APP") & 0xFFFF, {{0, 0xA1CE}})
                                      : wiz::U8(br.rom, {}, target);
                    std::string why = m.run(300000);
                    if (why.find("write to ROM") != std::string::npos ||
                        why.find("fault") != std::string::npos)
                        bad = p + ": " + why;
                    ran++;
                } catch (const std::exception& e) {
                    bad = p + ": " + e.what();
                }
            }
        }
        check("examples: every plain program runs without a fault", bad.empty(),
              bad.empty() ? std::to_string(ran) + " ran, " + std::to_string(skipped) +
                                " ROP/cwapp skipped"
                          : bad);
    }

    // ------------------------------------------------------------ directive grammar
    {
        check("directives: one table for every context",
              wiz::find_directive("org", wiz::DirectiveContext::Preprocess) &&
                  wiz::find_directive("org", wiz::DirectiveContext::Rop) &&
                  wiz::find_directive("pad", wiz::DirectiveContext::Rop));
        unsigned long v = 0;
        std::string err;
        bool bare = false;
        check("directives: decimal and 0x number grammar",
              wiz::parse_directive_number("256", v, err) && v == 256 &&
                  wiz::parse_directive_number("0x100", v, err) && v == 256);
        check("directives: bare hex is a flagged compatibility alias",
              wiz::parse_directive_number("E9E0", v, err, true, &bare) && v == 0xE9E0 && bare);
        check("directives: trailing junk is rejected",
              !wiz::parse_directive_number("12x", v, err) &&
                  err.find("expected a decimal") != std::string::npos);
        std::string out;
        check("directives: angle includes are rejected for project files",
              !wiz::parse_directive_string("<x.h>", out, err) &&
                  err.find("angle-bracket") != std::string::npos);
        bool all = true;
        for (const auto& d : wiz::directive_table())
            all = all && wiz::find_directive(d.name, d.context) != nullptr;
        check("directives: every table row resolves in its context", all);
        Err e = err_of("#for 999\nvoid main() { }\n");
        check("error: #for rejects an unknown target",
              e.ok && e.code == "preproc.directive-argument", e.msg);
        e = err_of("#include <missing.h>\nvoid main() { }\n");
        check("error: angle include must be a freestanding header",
              e.ok && e.code == "preproc.include-not-found", e.msg);
        e = err_of("#if 1\n#else\n#else\n#endif\nvoid main() { }\n");
        check("error: duplicate #else", e.ok && e.code == "preproc.unbalanced-conditional", e.msg);
        e = err_of("#define\nvoid main() { }\n");
        check("error: #define needs a name", e.ok && e.code == "preproc.directive-argument", e.msg);
        e = err_of("#line 0\nvoid main() { }\n");
        check("error: #line needs a positive number",
              e.ok && e.code == "preproc.directive-argument", e.msg);
    }
}
