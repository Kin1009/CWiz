// main_wizlint.cpp - the Wiz/CWiz linter driver.
//
//     wizlint [--target 580|880|cwapp] [--strict] [--json] file...
//
// Output is gcc-style and coloured on a TTY:
//     examples/01_hello.cwiz:7:5: warning: 'score' is declared but never read
//         [readability/unused-variable]
#include <cstdio>
#include <string>
#include <vector>

#include "build.hpp"
#include "cli.hpp"
#include "common.hpp"
#include "diag.hpp"
#include "lint.hpp"

using namespace wiz;

namespace {

const char* sev_name(LintSeverity sev) {
    switch (sev) {
        case LintSeverity::Error: return "error";
        case LintSeverity::Warning: return "warning";
        case LintSeverity::Note: return "note";
    }
    return "warning";
}

const char* sev_style(LintSeverity sev) {
    switch (sev) {
        case LintSeverity::Error: return style::ERROR;
        case LintSeverity::Warning: return style::WARNING;
        case LintSeverity::Note: return style::NOTE;
    }
    return style::WARNING;
}

std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    set_data_dir(autodetect_data_dir(argv[0]));

    std::vector<CliOption> opts = {
        {"--target", "target", true},
        {"--strict", "strict"},
        {"--json", "json"},
        {"--rules", "rules"},
        {"--color", "color", true, true, "always", {"auto", "always", "never"}},
        {"-h", "help"}, {"--help", "help"},
    };
    Cli a;
    std::string err;
    if (!parse_cli(argc, argv, opts, a, err)) {
        std::fprintf(stderr, "wizlint: error: %s (try --help)\n", err.c_str());
        return 2;
    }
    if (a.has("help") || (a.positional.empty() && !a.has("rules"))) {
        std::fputs(
            "wizlint - check Wiz (.wz) and CWiz (.cwiz) sources\n"
            "\n"
            "Usage:\n"
            "    wizlint [options] file...\n"
            "\n"
            "Options:\n"
            "    --target NAME   target used for the compiler pass (580, 880, cwapp)\n"
            "    --strict        exit 1 on warnings as well as errors\n"
            "    --json          machine-readable output (for editors)\n"
            "    --rules         list the rule ids\n"
            "    --color WHEN    auto (default), always, never\n",
            stdout);
        return a.has("help") ? 0 : 2;
    }
    if (a.has("rules")) {
        for (const LintRuleInfo& rule : lint_rules())
            std::fputs(fmt("%-32s %s\n", rule.rule, rule.description).c_str(), stdout);
        return 0;
    }

    set_color_mode_from_arg(a.value("color", "auto"));
    const bool tty = stdout_is_tty();
    const bool strict = a.has("strict");
    const bool json = a.has("json");

    std::string target = a.value("target");
    int errors = 0, warnings = 0, notes = 0;
    bool first_json = true;
    if (json) std::fputs("[", stdout);

    for (const std::string& path : a.positional) {
        bool ok = false;
        const std::string source = read_text_file(path, &ok);
        if (!ok) {
            if (json) {
                if (!first_json) std::fputs(",", stdout);
                first_json = false;
                std::fputs(fmt("{\"file\":\"%s\",\"line\":0,\"col\":0,\"severity\":\"error\","
                               "\"rule\":\"io\",\"message\":\"cannot read the file\"}",
                               json_escape(path).c_str())
                               .c_str(),
                           stdout);
            } else {
                std::fprintf(stderr, "%s: error: cannot read the file\n", path.c_str());
            }
            ++errors;
            continue;
        }
        const std::string file_target = target.empty() ? "580" : target;
        const std::vector<LintDiagnostic> diags = lint_source(source, path, file_target);
        for (const LintDiagnostic& d : diags) {
            switch (d.severity) {
                case LintSeverity::Error: ++errors; break;
                case LintSeverity::Warning: ++warnings; break;
                case LintSeverity::Note: ++notes; break;
            }
            if (json) {
                if (!first_json) std::fputs(",", stdout);
                first_json = false;
                std::fputs(fmt("{\"file\":\"%s\",\"line\":%d,\"col\":%d,\"severity\":\"%s\","
                               "\"rule\":\"%s\",\"message\":\"%s\"}",
                               json_escape(path).c_str(), d.line, d.col, sev_name(d.severity),
                               json_escape(d.rule).c_str(), json_escape(d.message).c_str())
                               .c_str(),
                           stdout);
            } else {
                std::string loc = path;
                if (d.line > 0) loc += fmt(":%d", d.line);
                if (d.col > 0) loc += fmt(":%d", d.col);
                std::fputs(fmt("%s: %s: %s [%s]\n", bold(loc, tty).c_str(),
                               paint(sev_name(d.severity), sev_style(d.severity), tty).c_str(),
                               d.message.c_str(), d.rule.c_str())
                               .c_str(),
                           stdout);
            }
        }
    }

    if (json) std::fputs("]\n", stdout);
    if (!json) {
        std::string summary = fmt("%d error(s), %d warning(s), %d note(s)", errors, warnings, notes);
        std::fputs(fmt("%s\n", paint(summary,
                                     errors ? style::ERROR : (warnings ? style::WARNING : style::OK),
                                     tty)
                                .c_str())
                       .c_str(),
                   stdout);
    }
    if (errors || (strict && warnings)) return 1;
    return 0;
}
