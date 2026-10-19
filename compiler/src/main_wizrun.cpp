// main_wizrun.cpp - wizrun: run a compiled Wiz / CWiz image in the simulator.
//
// wizc turns a source into a .bin (for a #cwapp source that is a complete,
// pre-patched fx-580VN X ROM).  wizrun just loads that image and runs it on the
// selected calculator model; the model defaults to 580.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "build.hpp"
#include "builtin_rom.hpp"
#include "cli.hpp"
#include "common.hpp"
#include "cpre.hpp"
#include "diag.hpp"
#include "patch.hpp"
#include "sim.hpp"
#include "targets.hpp"
#include "version.hpp"
#include "wiz.hpp"

using namespace wiz;

static const char* DOC =
    "wizrun - run a compiled Wiz / CWiz .bin on the simulator.\n"
    "\n"
    "    wizrun [options] file.bin [model]\n"
    "    wizrun [options] file.wz|file.cwiz [model]   (builds first)\n"
    "\n"
    "A .bin defaults to the 580 model.  A .cwiz/.wz source is compiled with the same\n"
    "front end as wizc; a #cwapp source is patched into the built-in 580 ROM and\n"
    "started directly.  A .bin is loaded as-is: if it carries the cwapp hook it runs\n"
    "the app, otherwise it runs the image from its reset vector.\n"
    "\n"
    "Options:\n"
    "  --target MODEL       calculator model: 580 or 880        (default: 580)\n"
    "  --steps N            maximum simulated instructions      (default: 3000000)\n"
    "  --keys LIST          keys held for the whole run, e.g. K_UP,K_OK or 0x20,0x28\n"
    "  --press LIST         timed key presses, e.g. K_OK@2000000,K_UP@3000000\n"
    "  --full               print the full 192x64 screen (default: 2:1 half resolution)\n"
    "  --color[=WHEN]       colour diagnostics: auto | always | never  (default: auto)\n"
    "  -v, --verbose        print extra detail\n"
    "  --version            print the version and exit\n"
    "  -h, --help           show this help and exit\n";

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

static bool is_binary_image(const std::string& path) {
    size_t dot = path.find_last_of('.');
    return dot != std::string::npos && to_lower(path.substr(dot)) == ".bin";
}

static bool source_has_cwapp(const std::string& path) {
    bool ok = false;
    std::string text = read_text_file(path, &ok);
    if (!ok) return false;
    try {
        return contains(declared_targets(text), "cwapp");
    } catch (...) {
        return false;
    }
}

// The patched ROM starts with the same trampoline wizc installs: B 3:0E40.
static bool is_patched_580(const std::vector<uint8_t>& rom) {
    return rom.size() == 262144 && rom[0x25E1E] == 0x00 && rom[0x25E1F] == 0xF3 &&
           rom[0x25E20] == 0x40 && rom[0x25E21] == 0x0E;
}

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    set_data_dir(autodetect_data_dir(argv[0]));

    std::vector<CliOption> opts = {
        {"--target", "target", true},
        {"--cwapp", "cwapp"},  // accepted from older scripts; now automatic
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
    if (args.positional.size() > 2) {
        std::fprintf(stderr, "wizrun: error: expected one file and an optional model\n");
        return 2;
    }

    set_color_mode_from_arg(args.value("color", "auto"));
    Diag d(program_name());

    std::string prog = args.positional[0];
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(prog, ec)) d.fatal(fmt("%s: no such file", prog.c_str()));

    // Model selection: an explicit --target, a second positional model, or 580.
    std::string target = args.value("target");
    bool explicit_model = !target.empty();
    if (args.positional.size() > 1) {
        auto model = canonical(args.positional[1]);
        if (!model) d.fatal(fmt("unknown model '%s' (use 580 or 880)", args.positional[1].c_str()));
        if (!target.empty() && canonical(target) != model)
            d.fatal("the model is given twice (positional and --target)");
        target = args.positional[1];
        explicit_model = true;
    }

    const bool binary = is_binary_image(prog);
    // A compiled image defaults to the 580; a source keeps wizc's default (880)
    // unless it declares only one machine or the user names a model.
    if (target.empty()) target = binary ? "580" : DEFAULT_TARGET;
    if (!binary && !explicit_model) {
        // Source convenience: honour a #cwapp section and a source that only
        // declares 880/580, otherwise use the source default.
        if (source_has_cwapp(prog)) {
            target = "cwapp";
        } else {
            bool ok = false;
            std::string text = read_text_file(prog, &ok);
            if (ok) {
                try {
                    auto declared = declared_targets(text);
                    if (contains(declared, "880") && !contains(declared, "580"))
                        target = "880";
                    else if (contains(declared, "580") && !contains(declared, "880"))
                        target = "580";
                } catch (...) {
                }
            }
        }
    }

    auto canon = canonical(target);
    if (!canon) d.fatal(fmt("unknown model '%s' (use 580 or 880)", target.c_str()));
    if (*canon == "cwapp") {
        if (binary) target = "580";
        // target "cwapp" means "compile the app"; the simulator runs it as a 580.
    }
    if (args.has("cwapp")) d.note("--cwapp is no longer needed; wizc/wizrun build the app automatically");

    BuildResult br;
    bool patched = false;
    if (binary) {
        bool ok = false;
        br.rom = read_binary_file(prog, &ok);
        if (!ok) d.fatal("cannot read: " + prog);
        patched = is_patched_580(br.rom);
        if (patched && canonical(target) == std::string("cwii"))
            d.fatal("this is a fx-580VN X #cwapp image, not an 880 program");
        if (patched) target = "580";
    } else {
        try {
            br = build_path(prog, target).r;
        } catch (const SectionError& e) {
            d.error(e.what(), prog);
            d.summary();
            return 1;
        } catch (const CompileError& e) {
            CwizPos p =
                language_of(prog) == "cwiz" ? cwiz_error_pos(prog, e.line) : CwizPos{prog, e.line};
            bool ok = false;
            std::string text = read_text_file(p.file, &ok);
            d.error(e.what(), p.file, p.line, e.col, ok ? source_line_at(text, p.line) : "");
            d.summary();
            return 1;
        } catch (const std::exception& e) {
            d.error(e.what(), prog);
            d.summary();
            return 1;
        }

        if (br.cwapp) {
            std::vector<uint8_t> rom(kFx580VnRom, kFx580VnRom + kFx580VnRomSize);
            RomPatchReport pr = patch_rom_image(std::move(rom), br, br.slot, false, true);
            for (const std::string& warning : pr.warnings) {
                if (pr.hook_forced && warning.rfind("set_mode at", 0) == 0) continue;
                d.note(warning, prog);
            }
            if (!pr.ok) {
                d.error(pr.error, prog, pr.error_line, pr.error_col,
                        pr.error_line > 0 ? source_line_at(read_text_file(prog), pr.error_line)
                                          : "");
                d.summary();
                return 1;
            }
            br.rom = std::move(pr.rom);
            patched = true;
            target = "580";
        }
    }

    // The simulator target name carries the core and LCD geometry.  The user-
    // facing model stays 580/880, so pick "cwapp" only for the app's row layout.
    const std::string sim_target = patched ? "cwapp" : (target == "880" ? "cwii" : "cw");
    const Target& tg = get_target(sim_target);

    int start_csr = -1, start_pc = -1;
    std::vector<std::pair<int, int>> stop;
    if (patched) {
        stop.push_back({0, 0xA1CE});
        auto it = br.syms.find("_APP");
        if (it != br.syms.end()) {
            start_csr = (it->second >> 16) & 0xFFFF;
            start_pc = it->second & 0xFFFF;
        } else {
            // A .bin has no symbols: enter the trampoline with cwapp's mode id.
            start_csr = 3;
            start_pc = 0x0E40;
        }
    }

    long steps = 3000000;
    if (args.has("steps")) steps = std::strtol(args.value("steps").c_str(), nullptr, 10);

    std::vector<std::pair<int, int>> keys;
    for (const std::string& t : split(args.value("keys"), ',')) {
        int ko = 0, ki = 0;
        if (parse_key(t, tg.name, d, ko, ki)) keys.push_back({ko, ki});
    }

    U8 m(br.rom, keys, tg.name, start_csr, start_pc, stop);
    if (patched && br.syms.find("_APP") == br.syms.end()) m.r[8] = 0xE0;

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
                   dim(fmt("(%s, stopped: %s, %ld steps)", patched ? "580 app" : target.c_str(),
                           why.c_str(), m.steps),
                       tty)
                       .c_str())
                   .c_str(),
               stdout);
    return 0;
}
