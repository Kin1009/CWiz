// main_wizc.cpp - wizc, the Wiz / CWiz compiler driver.
//
// Compiles Wiz (.wz) and CWiz (.cwiz) source for Casio ClassWiz calculators and
// writes a ROM image (.bin) plus an assembly listing (.asm).
#include <cstdio>
#include <string>
#include <vector>

#include "build.hpp"
#include "builtin_rom.hpp"
#include "cli.hpp"
#include "cpre.hpp"
#include "diag.hpp"
#include "patch.hpp"
#include "rop.hpp"
#include "version.hpp"
#include "wiz.hpp"

using namespace wiz;

static const char* DOC =
    "wizc - the Wiz / CWiz compiler driver.\n"
    "\n"
    "Compiles Wiz (.wz) and CWiz (.cwiz) source for Casio ClassWiz calculators and\n"
    "writes a ROM image (.bin) plus an assembly listing (.asm).\n"
    "A #rop source instead writes the exact RAC/hdcompiler payload to .bin\n"
    "and its launcher to .launcher.bin.\n"
    "\n"
    "A source with a #cwapp section is compiled for the fx-580VN X firmware and\n"
    "patched into the built-in 580 ROM automatically; #slot n chooses the menu slot\n"
    "(1..12, default 2).  The .bin is then a complete, runnable 256 KB ROM.\n"
    "\n"
    "    wizc [options] file...\n"
    "\n"
    "Options:\n"
    "  -o FILE              write the ROM image to FILE\n"
    "  --target MODEL       target calculator: 580 or 880   (default: by source)\n"
    "  --cwapp              force the fx-580VN X firmware app target (implies 580)\n"
    "  --lang LANG          force input language: wiz | cwiz   (default: by extension)\n"
    "  -S                   stop after assembly: write the .asm listing, no .bin\n"
    "  -E, --pp             preprocess only (expand directives) and print to stdout\n"
    "  --no-asm             do not write the .asm listing\n"
    "  --color[=WHEN]       colour diagnostics: auto | always | never  (default: auto)\n"
    "  -v, --verbose        print extra detail\n"
    "  --version            print the version and exit\n"
    "  -h, --help           show this help and exit\n"
    "\n"
    "Examples:\n"
    "  wizc hello.cwiz -o hello.bin\n"
    "  wizc --target 580 game.wz\n"
    "  wizc --target 880 --no-asm demo.cwiz\n"
    "\n"
    "Targets:\n"
    "  580   ML620906  nX-U16/100  192x63  1 bitplane\n"
    "  880   ML620909  nX-U8/100   192x64  2 bitplanes\n";

static std::string target_desc(const std::string& name) {
    if (name == "cw") return "580 ML620906 nX-U16/100";
    if (name == "cwii") return "880 ML620909 nX-U8/100";
    if (name == "cwapp") return "580 ML620906 nX-U16/100 (firmware patch)";
    return name;
}

static std::string resolve_target(const Cli& args, Diag& d, const std::string& src) {
    if (args.has("cwapp")) return "cwapp";
    if (args.has("target")) {
        auto name = canonical(args.value("target"));
        if (!name || *name == "cwapp")
            d.fatal(fmt("unknown target '%s' (use 580 or 880)", args.value("target").c_str()));
        return *name;
    }
    if (!src.empty()) {
        bool ok = false;
        std::string text = read_text_file(src, &ok);
        std::vector<std::string> declared;
        if (ok) {
            try {
                declared = declared_targets(text);
            } catch (...) {
                declared.clear();
            }
        }
        // A #cwapp section means "firmware app": build it and patch the built-in
        // 580 ROM unless the user explicitly selected another calculator above.
        if (contains(declared, "cwapp")) return "cwapp";
        if (declared.size() == 1) return declared[0];
        if (!declared.empty() && !contains(declared, DEFAULT_TARGET)) return declared[0];
    }
    return "";
}

static void output_paths(const std::string& src, const std::string& out, std::string& bin_path,
                         std::string& asm_path) {
    std::string base = splitext_root(out.empty() ? src : out);
    if (!out.empty()) {
        bin_path = out;
        base = splitext_root(bin_path);
    } else {
        bin_path = base + ".bin";
    }
    asm_path = base + ".asm";
}

static bool print_preprocessed(const std::string& src, const Cli& args, Diag& d) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) d.fatal(fmt("%s: no such file", src.c_str()));

    std::string target = resolve_target(args, d, src);
    const Target& tg = get_target(target.empty() ? DEFAULT_TARGET : target);
    bool ok = false;
    std::string text = read_text_file(src, &ok);
    if (!ok) d.fatal("cannot read file: " + src);
    if (source_is_rop(text)) {
        std::fputs(text.c_str(), stdout);
        return true;
    }
    try {
        if (language_of(src, args.value("lang")) == "cwiz")
            std::fputs(select_sections(preprocess_cwiz(text, src, tg.name), tg).c_str(), stdout);
        else
            std::fputs(select_sections(text, tg).c_str(), stdout);
    } catch (const CompileError& e) {
        CwizPos p = language_of(src, args.value("lang")) == "cwiz" ? cwiz_error_pos(src, e.line)
                                                                   : CwizPos{src, e.line};
        bool rok = false;
        std::string t2 = read_text_file(p.file, &rok);
        d.error(e.what(), p.file, p.line, e.col, rok ? source_line_at(t2, p.line) : "");
        return false;
    } catch (const std::exception& e) {
        d.error(e.what(), src);
        return false;
    }
    return true;
}

static bool compile_rop_one(const std::string& src, const Cli& args, Diag& d,
                            const std::string& text) {
    std::string target = resolve_target(args, d, src);
    const std::string model = target == "880" || target == "cwii" ? "880" : "580";
    RopResult rop;
    try {
        // build_rop is the native C++ implementation of the RAC/hdcompiler
        // method: the payload and launcher are separate output blocks.
        rop = build_rop(text, model);
    } catch (const std::exception& e) {
        d.error(e.what(), src);
        return false;
    }
    if (!rop.ok) {
        d.error(rop.error.empty() ? "ROP compilation failed" : rop.error, src);
        return false;
    }

    std::string bin_path, asm_path;
    output_paths(src, args.value("output"), bin_path, asm_path);
    const std::string launcher_path = splitext_root(bin_path) + ".launcher.bin";
    std::string listing = fmt("; ROP payload @ %04X (%zu bytes)\n; launcher @ %04X (%zu bytes)\n",
                               rop.base, rop.bytes.size(), rop.launcher_base, rop.launcher.size());
    listing += "; launcher: " + rop.launcher_hex + "\n" + rop.log;
    try {
        if (!args.has("no-asm") && !write_text_file(asm_path, listing))
            d.fatal("cannot write output: " + asm_path);
        if (!args.has("asm_only") && !write_binary_file(bin_path, rop.bytes))
            d.fatal("cannot write output: " + bin_path);
        if (!args.has("asm_only") && !write_binary_file(launcher_path, rop.launcher))
            d.fatal("cannot write output: " + launcher_path);
    } catch (const std::exception& e) {
        d.fatal(fmt("cannot write output: %s", e.what()));
    }

    const bool tty = stdout_is_tty();
    const char* label = args.has("asm_only") ? "AS" : "CC";
    const std::string out_desc = args.has("asm_only") ? asm_path : bin_path;
    std::fputs(fmt("%s %s -> %s\n", paint(label, style::OK, tty).c_str(), bold(src, tty).c_str(),
                   out_desc.c_str())
                   .c_str(),
               stdout);
    std::fputs(fmt("    [ROP %s] payload %zu bytes @ %04X; launcher %zu bytes @ %04X\n",
                   model.c_str(), rop.bytes.size(), rop.base, rop.launcher.size(), rop.launcher_base)
                   .c_str(),
               stdout);
    if (!args.has("asm_only"))
        std::fputs(fmt("    launcher -> %s\n", launcher_path.c_str()).c_str(), stdout);
    return true;
}

static bool compile_one(const std::string& src, const Cli& args, Diag& d) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_regular_file(src, ec)) d.fatal(fmt("%s: no such file", src.c_str()));

    bool source_ok = false;
    const std::string source_text = read_text_file(src, &source_ok);
    if (!source_ok) d.fatal("cannot read file: " + src);
    if (source_is_rop(source_text))
        return compile_rop_one(src, args, d, source_text);

    std::string target = resolve_target(args, d, src);

    BuildResult br;
    try {
        PathBuild pb = build_path(src, target, args.value("lang"));
        br = pb.r;
    } catch (const SectionError& e) {
        d.error(e.what(), src);
        return false;
    } catch (const CompileError& e) {
        CwizPos p = language_of(src, args.value("lang")) == "cwiz" ? cwiz_error_pos(src, e.line)
                                                                   : CwizPos{src, e.line};
        bool ok = false;
        std::string text = read_text_file(p.file, &ok);
        d.error(e.what(), p.file, p.line, e.col, ok ? source_line_at(text, p.line) : "");
        return false;
    } catch (const std::exception& e) {
        d.error(e.what(), src);
        return false;
    }

    for (const auto& note : last_notes()) d.note(note, src);

    std::string bin_path, asm_path;
    output_paths(src, args.value("output"), bin_path, asm_path);
    const Target& tg = get_target(target.empty() ? DEFAULT_TARGET : target);
    const bool asm_only = args.has("asm_only");

    // A #cwapp program is patched into the built-in fx-580VN X ROM right here,
    // so the .bin is a complete image that wizrun can load directly.
    bool patched = false;
    int patched_slot = br.slot;
    if (br.cwapp && !asm_only) {
        std::vector<uint8_t> rom(kFx580VnRom, kFx580VnRom + kFx580VnRomSize);
        RomPatchReport pr = patch_rom_image(std::move(rom), br, br.slot, false, true);
        for (const std::string& warning : pr.warnings) {
            // The bundled dump is a known variant; do not repeat the hook note.
            if (pr.hook_forced && warning.rfind("set_mode at", 0) == 0) continue;
            d.note(warning, src);
        }
        if (!pr.ok) {
            d.error(pr.error, src, pr.error_line, pr.error_col,
                    pr.error_line > 0 ? source_line_at(read_text_file(src), pr.error_line) : "");
            return false;
        }
        br.rom = std::move(pr.rom);
        patched = true;
        patched_slot = pr.slot;
    }

    try {
        if (!args.has("no-asm") && !write_text_file(asm_path, br.full_asm))
            d.fatal("cannot write output: " + asm_path);
        if (!asm_only && !write_binary_file(bin_path, br.rom))
            d.fatal("cannot write output: " + bin_path);
    } catch (const std::exception& e) {
        d.fatal(fmt("cannot write output: %s", e.what()));
    }

    const bool tty = stdout_is_tty();
    std::string summary = target_desc(tg.name);
    if (patched) summary += fmt(" slot %d", patched_slot);
    std::string out_desc = asm_only ? asm_path : bin_path;
    const char* label = asm_only ? "AS" : "CC";
    std::fputs(fmt("%s %s -> %s\n", paint(label, style::OK, tty).c_str(), bold(src, tty).c_str(),
                   out_desc.c_str())
                   .c_str(),
               stdout);
    std::string detail =
        fmt("[%s] %d variables, %d bytes ROM", summary.c_str(), (int)br.variables.size(),
            (int)br.rom.size());
    std::fputs(fmt("    %s\n", dim(detail, tty).c_str()).c_str(), stdout);
    return true;
}

int main(int argc, char** argv) {
    g_argv0 = argv[0];
    set_data_dir(autodetect_data_dir(argv[0]));

    std::vector<CliOption> opts = {
        {"-o", "output", true},
        {"--output", "output", true},
        {"--target", "target", true},
        {"--cwapp", "cwapp"},
        {"--lang", "lang", true, false, "", {"wiz", "cwiz"}},
        {"-S", "asm_only"},
        {"-E", "pp"},
        {"--pp", "pp"},
        {"--no-asm", "no-asm"},
        {"--color", "color", true, true, "always", {"auto", "always", "never"}},
        {"-v", "verbose"},
        {"--verbose", "verbose"},
        {"-Wall", "wall"},
        {"--version", "version"},
        {"-h", "help"},
        {"--help", "help"},
    };
    Cli args;
    std::string err;
    if (!parse_cli(argc, argv, opts, args, err)) {
        std::fprintf(stderr, "wizc: error: %s\n", err.c_str());
        return 2;
    }

    if (args.has("help") || (args.positional.empty() && !args.has("version"))) {
        std::fputs(DOC, stdout);
        return 0;
    }
    if (args.has("version")) {
        std::printf("wizc %s\n", WIZ_VERSION);
        return 0;
    }

    try {
        set_color_mode_from_arg(args.value("color", "auto"));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "wizc: error: %s\n", e.what());
        return 2;
    }
    Diag d(program_name());

    if (args.has("output") && args.positional.size() > 1)
        d.fatal("cannot use -o with multiple source files");
    if (args.has("cwapp")) {
        std::string t = args.value("target");
        if (args.has("target") && t != "580" && t != "cw")
            d.warning(fmt("--cwapp implies --target 580; ignoring --target %s", t.c_str()));
    }

    bool ok = true;
    for (const auto& src : args.positional) {
        if (args.has("pp")) ok = print_preprocessed(src, args, d) && ok;
        else ok = compile_one(src, args, d) && ok;
    }

    d.summary();
    return (ok && d.errors == 0) ? 0 : 1;
}
