// main_wizrun.cpp - wizrun: build a Wiz / CWiz program and run it in the simulator.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "build.hpp"
#include "cli.hpp"
#include "cpre.hpp"
#include "diag.hpp"
#include "sim.hpp"
#include "targets.hpp"
#include "version.hpp"
#include "wiz.hpp"

using namespace wiz;

static const char* DOC =
    "wizrun - build a Wiz (.wz) or CWiz (.cwiz) program and run it in the simulator.\n"
    "\n"
    "    wizrun [options] file...\n"
    "\n"
    "Options:\n"
    "  --target MODEL       target calculator: 580 or 880   (default: 880)\n"
    "  --cwapp              build a fx-580VN X firmware patch (implies --target 580)\n"
    "  --steps N            maximum simulated instructions   (default: 3000000)\n"
    "  --keys LIST          keys held for the whole run, e.g. K_UP,K_OK or 0x20,0x28\n"
    "  --press LIST         timed key presses, e.g. K_OK@2000000,K_UP@3000000\n"
    "  --full               print the full 192x64 screen (default: 2:1 half resolution)\n"
    "  --color[=WHEN]       colour diagnostics: auto | always | never   (default: auto)\n"
    "  -v, --verbose        print extra detail\n"
    "  --version            print the version and exit\n"
    "  -h, --help           show this help and exit\n"
    "\n"
    "The program is compiled with the same front end as `wizc` and executed on the\n"
    "nX-U8/nX-U16 simulator; the LCD is drawn to the terminal.\n";

// A key token is either a K_* name or a number (decimal or 0x..).
static bool parse_key(const std::string& token, const std::string& target, Diag& d, int& ko, int& ki) {
    std::string t = strip(token);
    if (t.empty()) return false;
    const std::map<std::string, int>& kc = key_consts(target);
    long long k;
    auto it = kc.find(t);
    if (it != kc.end()) {
        k = it->second;
        if (k < 0) d.fatal(fmt("key '%s' does not exist on this target", t.c_str()));
    } else {
        char* end = nullptr;
        k = std::strtoll(t.c_str(), &end, 0);
        if (end == t.c_str() || (end && *end != '\0'))
            d.fatal(fmt("unknown key '%s' (use a K_* name or a number)", t.c_str()));
    }
    ko = (int)(0x80 >> (k >> 3));
    ki = (int)(0x80 >> (k & 7));
    return true;
}

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    set_data_dir(autodetect_data_dir(argv[0]));

    std::vector<CliOption> opts = {
        {"--target", "target", true},
        {"--cwapp", "cwapp"},
        {"--steps", "steps", true},
        {"--keys", "keys", true},
        {"--press", "press", true},
        {"--full", "full"},
        {"--color", "color", true, true, "always", {"auto", "always", "never"}},
        {"-v", "verbose"},
        {"--verbose", "verbose"},
        {"--version", "version"},
        {"-h", "help"},
        {"--help", "help"},
    };
    Cli args;
    std::string err;
    if (!parse_cli(argc, argv, opts, args, err)) {
        std::fprintf(stderr, "wizrun: error: %s\n", err.c_str());
        return 2;
    }

    if (args.has("help") || (args.positional.empty() && !args.has("version"))) {
        std::fputs(DOC, stdout);
        return 0;
    }
    if (args.has("version")) {
        std::printf("wizrun %s\n", WIZ_VERSION);
        return 0;
    }
    if (args.positional.empty()) {
        std::fprintf(stderr, "wizrun: error: no program given\n");
        return 2;
    }

    set_color_mode_from_arg(args.value("color", "auto"));
    Diag d(program_name());

    std::string prog = args.positional[0];
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(prog, ec)) d.fatal(fmt("%s: no such file", prog.c_str()));

    std::string target = args.has("cwapp") ? "cwapp" : args.value("target");
    if (!target.empty() && target != "cwapp" && !canonical(target))
        d.fatal(fmt("unknown target '%s' (use 580 or 880)", target.c_str()));

    std::string lang = language_of(prog);
    BuildResult br;
    try {
        br = build_path(prog, target).r;
    } catch (const SectionError& e) {
        d.error(e.what(), prog);
        return d.summary() ? 1 : 1;
    } catch (const CompileError& e) {
        CwizPos p = language_of(prog) == "cwiz" ? cwiz_error_pos(prog, e.line) : CwizPos{prog, e.line};
        bool ok = false;
        std::string text = read_text_file(p.file, &ok);
        d.error(e.what(), p.file, p.line, e.col, ok ? source_line_at(text, p.line) : "");
        return d.summary() ? 1 : 1;
    } catch (const std::exception& e) {
        d.error(e.what(), prog);
        return d.summary() ? 1 : 1;
    }
    (void)lang;

    const Target& tg = get_target(target.empty() ? DEFAULT_TARGET : target);

    int start_csr = -1, start_pc = -1;
    std::vector<std::pair<int, int>> stop;
    if (tg.name == "cwapp") {
        auto it = br.syms.find("_APP");
        int appv = it == br.syms.end() ? 0 : it->second;
        start_csr = (appv >> 16) & 0xFFFF;
        start_pc = appv & 0xFFFF;
        stop.push_back({0, 0xA1CE});
    }

    long steps = 3000000;
    if (args.has("steps")) steps = std::strtol(args.value("steps").c_str(), nullptr, 10);

    std::vector<std::pair<int, int>> keys;
    for (const std::string& t : split(args.value("keys"), ',')) {
        int ko = 0, ki = 0;
        if (parse_key(t, tg.name, d, ko, ki)) keys.push_back({ko, ki});
    }

    U8 m(br.rom, keys, tg.name, start_csr, start_pc, stop);

    std::vector<std::pair<long, std::pair<int, int>>> events;
    for (const std::string& item : split(args.value("press"), ',')) {
        if (item.empty()) continue;
        size_t at = item.find('@');
        if (at == std::string::npos)
            d.fatal(fmt("--press needs NAME@STEP, got '%s'", item.c_str()));
        int ko = 0, ki = 0;
        parse_key(item.substr(0, at), tg.name, d, ko, ki);
        events.push_back({std::strtol(item.substr(at + 1).c_str(), nullptr, 10), {ko, ki}});
    }
    std::sort(events.begin(), events.end());

    std::string why;
    if (events.empty()) {
        why = m.run(steps);
    } else {
        for (const auto& ev : events) {
            why = m.run(ev.first);
            m.keys.push_back(ev.second);
        }
        why = m.run(steps);
    }

    std::vector<std::string> art = split(m.ascii(), '\n');
    if (!args.has("full")) {
        std::vector<std::string> rows;
        for (size_t i = 0; i < art.size(); i += 2) rows.push_back(art[i]);
        std::vector<std::string> cols;
        for (auto& l : rows) {
            std::string s;
            for (size_t i = 0; i < l.size(); i += 2) s += l[i];
            cols.push_back(s);
        }
        art = cols;
    }
    std::fputs((join(art, "\n") + "\n").c_str(), stdout);

    const bool tty = stdout_is_tty();
    std::fputs(fmt("%s %s  %s\n", green("ran", tty).c_str(), prog.c_str(),
                   dim(fmt("(stopped: %s, %ld steps)", why.c_str(), m.steps), tty).c_str())
                   .c_str(),
               stdout);
    return 0;
}
