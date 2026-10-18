// test_cpre.cpp - CWiz preprocessor and pointer tests.
//
// The directives are checked on the preprocessor's own output (exact text and
// line preservation), and then end-to-end through build_cwiz + the simulator.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "cpre.hpp"
#include "harness.hpp"

using namespace wt;

namespace {

std::string pp(const std::string& src, const std::string& target = "cwii",
               const std::string& file = "t.cwiz") {
    return wiz::preprocess_cwiz(src, file, target);
}

struct PpErr {
    bool ok = false;
    std::string msg;
};
PpErr pp_error(const std::string& src, const std::string& needle,
               const std::string& target = "cwii", const std::string& file = "t.cwiz") {
    try {
        wiz::preprocess_cwiz(src, file, target);
    } catch (const std::exception& e) {
        std::string s = e.what();
        return {s.find(needle) != std::string::npos, s};
    }
    return {false, "no error"};
}

// Compile + run CWiz source through the real pipeline (preprocessor included).
Machine run_cwiz_src(const std::string& src, const std::string& target) {
    wiz::BuildResult br = wiz::build_cwiz(src, target);
    wiz::U8 m(br.rom, {}, target);
    std::string why = m.run(800000);
    return Machine{std::move(m), br.variables, why, br.rom, br.full_asm, br.syms};
}

bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

int nlines(const std::string& s) {
    int n = 0;
    for (char c : s)
        if (c == '\n') n++;
    return n;
}

}  // namespace

void wt::run_cpre() {
    // ----------------------------------------------------------------- text
    {
        std::string out = pp("#define N 5\nint x = N;");
        check("#define object-like expands", has(out, "int x = 5;"), out);
    }
    {
        std::string out = pp("#define W 40 // forty\n#define H /* three */ 3\nint a = W; int b = H;");
        check("a comment on a #define line is not part of the body",
              has(out, "int a = 40;") && has(out, "int b = 3;"), out);
    }
    {
        std::string out = pp("/*\n#define NOPE 1\n*/\n#ifdef NOPE\nint bad;\n#endif\nint a = 1;");
        check("a directive inside a block comment is ignored",
              !has(out, "int bad;") && has(out, "int a = 1;"), out);
    }
    {
        std::string out = pp("#define SQ(x) ((x) * (x))\nint y = SQ(3) + SQ(2 + 1);");
        check("function-like macro expands with arguments",
              has(out, "int y = ((3) * (3)) + ((2 + 1) * (2 + 1));"), out);
    }
    {
        std::string out = pp("#define M 1\n#define M 2\nint a = M;");
        check("macro redefinition replaces the old body", has(out, "int a = 2;"), out);
    }
    {
        std::string out = pp("#define M 1\n#undef M\nint a = M;");
        check("#undef removes the macro", has(out, "int a = M;"), out);
    }
    {
        std::string out = pp("#define A 1\n#define B A + 1\nint z = B;");
        check("macros expand recursively", has(out, "int z = 1 + 1;"), out);
    }
    {
        std::string out = pp("text(0, 0, \"N\"); // N\n#define N 9");
        check("macros do not expand inside strings or comments",
              has(out, "\"N\"") && has(out, "// N"), out);
    }
    {
        std::string out = pp("#define LONG \\\n    1 + \\\n    2\nint q = LONG;");
        check("backslash continues a directive",
              has(out, "int q = 1 + 2;") && nlines(out) == nlines("#define LONG \\\n    1 + \\\n    2\nint q = LONG;"),
              out);
    }

    // ----------------------------------------------------------------- conditionals
    {
        std::string out = pp("#ifdef __CWIZ__\nint yes;\n#else\nint no;\n#endif");
        check("#ifdef sees a predefined macro",
              has(out, "int yes;") && !has(out, "int no;"), out);
    }
    {
        std::string out = pp("#ifdef NOPE\nint no;\n#else\nint yes;\n#endif");
        check("#else runs when the condition is false",
              has(out, "int yes;") && !has(out, "int no;"), out);
    }
    {
        std::string out = pp("#if 2 + 3 * 4 == 14\nint yes;\n#endif");
        check("#if evaluates arithmetic with C precedence", has(out, "int yes;"), out);
    }
    {
        std::string out = pp("#define K 3\n#if defined(K) && !defined(J)\nint yes;\n#endif");
        check("#if understands defined() and !", has(out, "int yes;"), out);
    }
    {
        std::string out = pp("#if 0\nint a;\n#elif 1\nint b;\n#else\nint c;\n#endif");
        check("#elif picks the first true branch",
              !has(out, "int a;") && has(out, "int b;") && !has(out, "int c;"), out);
    }
    {
        std::string out = pp("#if 1\n#if 0\nint a;\n#else\nint b;\n#endif\n#endif");
        check("nested conditionals", has(out, "int b;") && !has(out, "int a;"), out);
    }
    {
        std::string out = pp("#if 0\n#if 1\nint a;\n#endif\nint b;\n#endif\nint c;");
        check("an inactive outer block hides an inner one",
              !has(out, "int a;") && !has(out, "int b;") && has(out, "int c;"), out);
    }
    {
        std::string out = pp("#if 0\n#error should not fire\n#endif\nint ok;");
        check("#error in a dead branch is ignored", has(out, "int ok;"), out);
    }
    {
        std::string out = pp("#if 1 << 3 == 8\nint shifted;\n#endif");
        check("#if supports <<", has(out, "int shifted;"), out);
    }

    // ----------------------------------------------------------------- predefined
    {
        std::string a = pp("#ifdef __CW__\nint cw;\n#else\nint notcw;\n#endif", "cw");
        std::string b = pp("#ifdef __CW__\nint cw;\n#else\nint notcw;\n#endif", "cwii");
        check("__CW__ is defined only for the 580 target",
              has(a, "int cw;") && has(b, "int notcw;"), a + "|" + b);
    }
    {
        std::string a = pp("int n = __ROWS__;", "cw");
        std::string b = pp("int n = __ROWS__;", "cwii");
        check("__ROWS__ carries the target row count",
              has(a, "= 63;") && has(b, "= 64;"), a + "|" + b);
    }

    // ----------------------------------------------------------------- #for
    {
        std::string out = pp("#for 580\nint small;\n#for 880\nint big;");
        check("#for becomes a target section header",
              has(out, "for 580") && has(out, "for 880"), out);
    }
    {
        std::string out = pp("#for 580\nint small;\n#cwapp\nint app;\n"
                             "----\nint every;");
        check("#for 580 and the #cwapp marker become target sections",
              has(out, "for 580") && has(out, "for cwapp") && has(out, "int every;"), out);
    }
    {
        // The sections are checked on the selected text: running a cwapp image
        // needs the firmware's menu mapping, which the simulator does not set up
        // here (see `wizrun --cwapp`).
        std::string src =
            "int R;\n"
            "#for 580\n"
            "void leave() { R = 1; }\n"
            "#cwapp\n"
            "void leave() { R = 2; }\n"
            "----\n"
            "void main() { leave(); }\n";
        std::string cw = wiz::select_sections(wiz::preprocess_cwiz(src, "t.cwiz", "cw"),
                                              wiz::get_target("cw"));
        std::string app = wiz::select_sections(wiz::preprocess_cwiz(src, "t.cwiz", "cwapp"),
                                               wiz::get_target("cwapp"));
        check("#for 580 and the #cwapp app marker select one section each",
              has(cw, "R = 1;") && !has(cw, "R = 2;") && has(app, "R = 2;") && !has(app, "R = 1;"),
              cw + " |||| " + app);
        // The cwapp build is a real, loadable image.
        wiz::BuildResult b = wiz::build_cwiz(src, "cwapp");
        check("the #cwapp section builds a cwapp image", b.rom.size() > 100000,
              std::to_string(b.rom.size()));
    }
    {
        Machine r = run_cwiz_src("int R;\n"
                                 "#for 580\n"
                                 "void main() { R = 1; }\n"
                                 "#for 880\n"
                                 "void main() { R = 2; }\n",
                                 "cw");
        check("#for 580 section is selected on the 580 target",
              r.vars.count("R") && var_of(r.m, r.vars, "R") == 1, r.why);
        Machine r2 = run_cwiz_src("int R;\n"
                                  "#for 580\n"
                                  "void main() { R = 1; }\n"
                                  "#for 880\n"
                                  "void main() { R = 2; }\n",
                                  "cwii");
        check("#for 880 section is selected on the 880 target",
              r2.vars.count("R") && var_of(r2.m, r2.vars, "R") == 2, r2.why);
    }

    // ----------------------------------------------------------------- line numbers
    {
        std::string in = "#define N 1\n\nint a = N;\n#if 0\nx\n#endif\ny;\n";
        check("the output keeps the input line count", nlines(pp(in)) == nlines(in),
              std::to_string(nlines(pp(in))) + " vs " + std::to_string(nlines(in)));
    }

    // ----------------------------------------------------------------- errors
    {
        PpErr e = pp_error("#error broken build", "broken build");
        check("#error reports its message", e.ok, e.msg);
    }
    {
        PpErr e = pp_error("#if 1\nint a;", "missing #endif");
        check("unterminated #if is an error", e.ok, e.msg);
    }
    {
        PpErr e = pp_error("#endif", "#endif without #if");
        check("#endif without #if is an error", e.ok, e.msg);
    }
    {
        PpErr e = pp_error("#wibble", "unknown directive '#wibble'");
        check("an unknown directive is an error", e.ok, e.msg);
    }
    {
        PpErr e = pp_error("#define\nint a;", "#define needs a name");
        check("#define without a name is an error", e.ok, e.msg);
    }
    {
        PpErr e = pp_error("#if 1/0\nint a;\n#endif", "division by zero");
        check("#if division by zero is an error", e.ok, e.msg);
    }

    // ----------------------------------------------------------------- #include
    {
        std::string dir = (std::filesystem::temp_directory_path() / "wizcpre").string();
        std::filesystem::create_directories(dir);
        {
            std::ofstream f(dir + "/inc.cwiz");
            f << "#pragma once\n#define FROM_INC 42\nint inc_var = FROM_INC;\n";
        }
        {
            std::ofstream f(dir + "/loop.cwiz");
            f << "#include \"loop.cwiz\"\n";
        }
        std::string src = "#include \"inc.cwiz\"\n#include \"inc.cwiz\"\nint use = FROM_INC;";
        std::string out = pp(src, "cwii", dir + "/main.cwiz");
        std::vector<wiz::CwizOrigin> origins;
        for (int ln = 1; ln <= nlines(out) + 2; ln++) origins.push_back(wiz::cwiz_line_origin(ln));
        check("#include inlines once and #pragma once guards it",
              has(out, "int use = 42;") && out.find("int inc_var") == out.rfind("int inc_var"), out);

        PpErr e = pp_error("#include \"loop.cwiz\"", "#include cycle", "cwii", dir + "/main2.cwiz");
        check("#include cycle is an error", e.ok, e.msg);

        PpErr miss = pp_error("#include \"nope_missing.cwiz\"", "cannot read #include", "cwii",
                              dir + "/main3.cwiz");
        check("a missing #include is an error", miss.ok, miss.msg);

        std::string origin;
        for (const auto& o : origins)
            if (has(o.file, "inc.cwiz")) origin = o.file + ":" + std::to_string(o.line);
        check("cwiz_line_origin() maps an included line", has(origin, "inc.cwiz"), origin);
        std::filesystem::remove_all(dir);
    }

    // ----------------------------------------------------------------- compiled end to end
    {
        Machine r = run_cwiz_src("#define W 4\n"
                                 "#define H 3\n"
                                 "#define AREA (W * H)\n"
                                 "int R;\n"
                                 "#if AREA == 12\n"
                                 "void main() { R = AREA + 100; }\n"
                                 "#else\n"
                                 "#error bad AREA\n"
                                 "#endif\n",
                                 "cwii");
        check("a program built from directives runs (#if branch)",
              r.vars.count("R") && var_of(r.m, r.vars, "R") == 112, r.why);
        Machine r2 = run_cwiz_src("int R;\n"
                                  "#ifdef __CWII__\n"
                                  "void main() { R = 7; }\n"
                                  "#endif\n"
                                  "#ifdef __CW__\n"
                                  "void main() { R = 8; }\n"
                                  "#endif\n",
                                  "cwii");
        check("a program can pick its main() per target",
              r2.vars.count("R") && var_of(r2.m, r2.vars, "R") == 7, r2.why);
    }
}
