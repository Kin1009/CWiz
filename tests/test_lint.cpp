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
