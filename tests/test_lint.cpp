// test_lint.cpp - wizlint core tests.
#include <cstring>
#include <string>
#include <vector>

#include "font6x8.hpp"
#include "harness.hpp"
#include "lint.hpp"

using namespace wt;

namespace {

const wiz::LintDiagnostic* find_rule(const std::vector<wiz::LintDiagnostic>& diags,
                                     const std::string& rule) {
    for (const auto& d : diags)
        if (d.rule == rule) return &d;
    return nullptr;
}

bool has(const std::vector<wiz::LintDiagnostic>& diags, const std::string& rule) {
    return find_rule(diags, rule) != nullptr;
}

std::vector<wiz::LintDiagnostic> lint(const std::string& src, const std::string& file = "t.cwiz",
                                      const std::string& target = "cwii") {
    return wiz::lint_source(src, file, target);
}

}  // namespace

void wt::run_lint() {
    // A clean program has no findings.
    {
        auto d = lint("int main() {\n    return 0;\n}\n");
        check("lint: clean CWiz program is quiet", d.empty(),
              d.empty() ? "" : d[0].rule + ": " + d[0].message);
    }

    // Declared but never read.
    {
        auto d = lint("int unused = 0;\nint main() { return 0; }\n");
        const auto* hit = find_rule(d, "readability/unused-variable");
        check("lint: unused variable", hit && hit->line == 1);
    }

    // A used variable is not reported.
    {
        auto d = lint("int used = 0;\nint main() { used = 1; return used; }\n");
        check("lint: used variable is not unused", !has(d, "readability/unused-variable"));
    }

    // Locals hiding a global.
    {
        auto d = lint("int value = 1;\nint main() {\n    int value = 2;\n    return value;\n}\n");
        const auto* hit = find_rule(d, "correctness/shadowed-variable");
        check("lint: shadowed global", hit && hit->line == 3);
    }

    // Nothing can follow return in the same block.
    {
        auto d = lint("int main() {\n    return 0;\n    return 1;\n}\n");
        const auto* hit = find_rule(d, "correctness/unreachable-code");
        check("lint: unreachable statement", hit && hit->line == 3);
    }

    // Style rules.
    {
        std::string long_line = "int main() { return 0; } // " + std::string(120, 'x') + "\n";
        auto d = lint(long_line);
        check("lint: long line", has(d, "style/line-length"));
    }
    {
        auto d = lint("int main() {   \n    return 0;\n}\n");
        check("lint: trailing whitespace", has(d, "style/trailing-whitespace"));
    }
    {
        auto d = lint("int main() {\n\treturn 0;\n}\n");
        check("lint: tab indentation", has(d, "style/tabs"));
    }
    {
        auto d = lint("// TODO: finish this\nint main() { return 0; }\n");
        check("lint: todo marker", has(d, "style/todo"));
    }

    // Preprocessor sections.
    {
        auto d = lint("#for 880\n#for 880\nint main() { return 0; }\n");
        check("lint: duplicate #for", has(d, "preproc/duplicate-target"));
    }
    {
        auto d = lint("#for 880\n#cwapp\nint main() { return 0; }\n");
        check("lint: #cwapp after the wrong target",
              has(d, "compile") && find_rule(d, "compile")->line == 2);
    }
    {
        auto d = lint("#for 580\n#cwapp\n#slot 3\nint main() { return 0; }\n", "t.cwiz", "cwapp");
        check("lint: #slot after #cwapp is accepted",
              !has(d, "preproc/slot-order") && !has(d, "preproc/slot-range"), "");
    }
    {
        auto d = lint("#slot 3\nint main() { return 0; }\n", "t.cwiz", "cwapp");
        check("lint: #slot before #cwapp", has(d, "preproc/slot-order"));
    }
    {
        auto d = lint("#for 580\n#cwapp\n#slot 13\nint main() { return 0; }\n", "t.cwiz", "cwapp");
        check("lint: #slot outside 1..12", has(d, "preproc/slot-range"));
    }
    {
        auto d = lint("#for 580\n#cwapp\n#slot 2\n#slot 3\nint main() { return 0; }\n", "t.cwiz",
                      "cwapp");
        check("lint: duplicate #slot", has(d, "preproc/duplicate-slot"));
    }

    // ROP directives are handled by the ROP backend, not the ordinary CWiz
    // preprocessor.  In particular, #org and #launcher must not be reported as
    // unknown directives by the linter.
    {
        const std::string rop =
            "#rop\n#for 580\n#org E9E0\n#launcher common\n"
            "int main() { screen_del(); print(0x30, 0x01, \"LINT ROP\"); render(); }\n";
        auto d = lint(rop, "t.cwiz", "cw");
        check("lint: ROP directives compile", !has(d, "compile"),
              d.empty() ? "" : d[0].rule + ": " + d[0].message);
    }

    // The 580 text and graphics builtins have their own ABI and must not be
    // reported as unknown functions.
    {
        const std::string rop =
            "#rop\n#for 580\n#org E9E0\n#launcher common\n"
            "int main() {\n"
            "    setlr();\n"
            "    setsfr();\n"
            "    screen_del();\n"
            "    print(0x30, 0x01, \"LINT ROP\");\n"
            "    print_small(0x0E, 0x11, \"SMALL\");\n"
            "    line(0, 0, 192, 63);\n"
            "    rect(1, 2, 3, 4);\n"
            "    pixel(5, 6, 1);\n"
            "    bitmap(10, 10, 8, 8, \"ff818181818181ff\");\n"
            "    delay(0x1F40);\n"
            "    waitshift();\n"
            "    set_byte(0xD138, 1);\n"
            "    set_word(0xD138, 0x1234);\n"
            "    add_word(0xF039, 0x0102);\n"
            "    memcpy(0xD630, 0xD184, 0x2FE);\n"
            "    strcpy(0xD630, 0xD184);\n"
            "    getkey();\n"
            "    keytable(0x0480, up, 0x0000, done);\n"
            "up:\n"
            "    goto done;\n"
            "done:\n"
            "    render();\n"
            "}\n";
        auto d = lint(rop, "t.cwiz", "cw");
        check("lint: ROP builtins compile", !has(d, "compile"),
              d.empty() ? "" : d[0].rule + ": " + d[0].message);
    }

    // The text may also be the first argument of print / print_small.
    {
        const std::string rop =
            "#rop\n#for 580\n#org E9E0\n#launcher common\n"
            "int main() { print(\"SWAP\", 0x30, 0x01); print_small(\"SWAP\", 0x0E, 0x11); }\n";
        auto d = lint(rop, "t.cwiz", "cw");
        check("lint: ROP print accepts the text first", !has(d, "compile"),
              d.empty() ? "" : d[0].rule + ": " + d[0].message);
    }

    // The community launcher takes a length code and the backup / execution
    // addresses; the linter must accept them.
    {
        const std::string rop =
            "#rop\n#for 580\n#org E9E0\n#launcher common long E9E0 D730\n"
            "int main() { render(); }\n";
        auto d = lint(rop, "t.cwiz", "cw");
        check("lint: ROP launcher options compile", !has(d, "compile"),
              d.empty() ? "" : d[0].rule + ": " + d[0].message);
    }

    // Compiler errors are part of the linter output.
    {
        auto d = lint("int main() { return does_not_exist; }\n");
        check("lint: compiler error", has(d, "compile"));
    }

    // Wiz (Python-like) assignments are declarations too.
    {
        auto d = lint("def main():\n    x = 1\n", "t.wz", "cwii");
        check("lint: unused Wiz variable", has(d, "readability/unused-variable"));
    }

    // The compiled-program font is the original Wiz 6x8 ASCII font
    // (font6x8.py, libcw.c YsFont6x8).  These are the glyph '0' bytes; a
    // regression to any other table changes them, so this documents the intent.
    {
        const unsigned char zero[8] = {0x70, 0x88, 0x98, 0xA8, 0xC8, 0x88, 0x70, 0x00};
        check("lint/font: compiled programs use the CW glyphs",
              std::memcmp(FONT6X8 + (0x30 - 32) * 8, zero, 8) == 0);
    }
}
