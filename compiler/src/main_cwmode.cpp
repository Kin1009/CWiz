// main_cwmode.cpp - add modes to a Casio fx-580VN X or fx-880BTG ROM.
//
// The two reference tools are interactive Windows programs.  This driver keeps
// the same prompts under --interactive and adds a scriptable form:
//
//   cwmode --model 580 rom.bin out.bin --modes 1 \
//          --code E1 --icon app.png --name "APP" --name "APP" --entry 31000
//
//   cwmode --model 880 rom.bin out.bin --modes 1 \
//          --code E1 --icon app.png --name text0.png --name text1.png --entry 30000
//
// The input ROM is never modified.  Nothing is written if validation fails.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "common.hpp"
#include "cwmode.hpp"

using namespace wiz;
namespace fs = std::filesystem;

namespace {

const char* kUsage =
    "cwmode - add extra modes to an fx-580VN X (580) or fx-880BTG (880) ROM\n"
    "\n"
    "Usage:\n"
    "    cwmode --model 580|880 IN.bin OUT.bin --modes N\n"
    "           --code HEX --icon PNG [--name TEXT|PNG]... [--entry ADDR]\n"
    "           [--code HEX ...]\n"
    "    cwmode --model 580|880 IN.bin [--info]\n"
    "    cwmode --model 580|880 IN.bin OUT.bin --interactive\n"
    "\n"
    "  --model 580|880    firmware family (required)\n"
    "  --modes N          number of modes that follow (required unless --interactive)\n"
    "  --code HEX         mode id byte; starts a new mode (E1, 0xE1)\n"
    "  --icon PNG         mode icon: 48x23 on the 580, 64x16 on the 880\n"
    "  --name TEXT        580: menu name, repeat for language 0 and 1\n"
    "                     880: 64x13 text bitmap, repeat twice\n"
    "  --name-image PNG   same as --name for the 880 (alias)\n"
    "  --entry ADDR       mode entry address (hex)\n"
    "  --at ADDR          write offset (default: first long erased run)\n"
    "                     the 880 also accepts 0x70000..0x71FFF\n"
    "  --force            overwrite a non-blank target range\n"
    "  --info             print the discovered signatures and exit\n"
    "  --interactive      ask the reference tool's questions\n"
    "  --json             machine-readable summary\n"
    "  --verbose          dump every generated payload (like the reference)\n"
    "  -h, --help\n"
    "\n"
    "ROM sizes: 580 = 262144 bytes, 880 >= 393216 bytes (only the 0x60000 code\n"
    "window is patched; larger dumps keep their tail).\n"
    "Examples:\n"
    "  cwmode --model 580 stock.bin patched.bin --modes 1 --code E1 \\\n"
    "         --icon icon48.png --name \"APP\" --name \"APP\" --entry 31000\n";

bool parse_hex(const std::string& text, uint32_t& out) {
    size_t i = 0;
    while (i < text.size() && std::isspace((unsigned char)text[i])) ++i;
    bool hex = true;
    if (i + 1 < text.size() && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        i += 2;
    } else if (i < text.size() && (std::isalpha((unsigned char)text[i]) || text[i] == '?')) {
        hex = true;
    }
    uint64_t value = 0;
    size_t digits = 0;
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (std::isspace((unsigned char)c)) break;
        int d;
        if (hex) {
            if (c >= '0' && c <= '9')
                d = c - '0';
            else if (c >= 'a' && c <= 'f')
                d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                d = c - 'A' + 10;
            else
                return false;
        } else {
            if (c < '0' || c > '9') return false;
            d = c - '0';
        }
        value = value * (hex ? 16 : 10) + (uint64_t)d;
        if (value > 0xFFFFFF) return false;
        ++digits;
    }
    if (!digits) return false;
    out = (uint32_t)value;
    return true;
}

bool parse_dec(const std::string& text, long& out) {
    char* end = nullptr;
    const long v = std::strtol(text.c_str(), &end, 10);
    if (!end || *end != '\0') return false;
    out = v;
    return true;
}

std::string ask(const std::string& prompt) {
    if (!prompt.empty()) {
        std::fputs(prompt.c_str(), stdout);
        std::fputs("\n", stdout);
    }
    std::fflush(stdout);
    std::string line;
    if (!std::getline(std::cin, line)) return std::string();
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

std::string hex_addr(uint32_t addr) { return fmt("%X:%04X", (addr >> 16) & 0xFFFF, addr & 0xFFFF); }

void print_info(const cwmode::Analysis& a) {
    std::printf("model           %s\n", cwmode::model_name(a.model));
    std::printf("rom             %u bytes (window %X)\n", a.rom_size, a.window);
    std::printf("mode count      %u at %04X\n", a.mode_count, a.mode_num_addr);
    std::printf("main entry      %s -> %05X\n", hex_addr(a.main_entry).c_str(), a.main_func);
    std::printf("main hook       %s (comp %05X)\n", hex_addr(a.main_hook).c_str(), a.comp_func);
    std::printf("menu hook       %s\n", hex_addr(a.menu_hook).c_str());
    std::printf("mode hooks      %s, %s\n", hex_addr(a.mode_hook_1).c_str(),
                hex_addr(a.mode_hook_2).c_str());
    std::printf("mode table      %04X\n", a.sys_mode_table);
    std::printf("string table    %04X\n", a.sys_str_table);
    std::printf("code end        %s (%u bytes free)\n", hex_addr(a.code_end).c_str(),
                a.window - a.code_end);
    if (a.model == cwmode::Model::Cw580)
        std::printf("helpers         mul %05X  unknown %05X  line-print %05X\n", a.mul_func,
                    a.unknown_func, a.line_print_func);
}

// ---------------------------------------------------------------- interactive
int run_interactive(const std::vector<uint8_t>& rom, const cwmode::Analysis& a,
                    cwmode::Options options, const std::string& out_path, bool verbose) {
    std::printf("[ + ] Patch from %s (Y/N)\n", hex_addr(a.code_end).c_str());
    std::string answer = ask("");
    char first = 'Y';
    for (char c : answer)
        if (!std::isspace((unsigned char)c)) {
            first = (char)std::toupper((unsigned char)c);
            break;
        }
    if (first == 'N') {
        std::printf("[ * ] Input target addr:\n");
        uint32_t at = 0;
        const std::string line = ask("");
        if (!parse_hex(line, at)) {
            std::fprintf(stderr, "cwmode: error: invalid address\n");
            return 1;
        }
        options.at = at;  // the library applies the 880 +0x12000 rule
    }
    std::printf("[ * ] Number of modes to add:\n");
    long count = 0;
    const std::string count_line = ask("");
    if (!parse_dec(count_line, count) || count < 1 || count > 64) {
        std::fprintf(stderr, "cwmode: error: the mode count must be 1..64\n");
        return 1;
    }
    if (a.model == cwmode::Model::Cw580)
        std::printf("[ + ] Note: string beginning with '*' is recognized as hex values.\n");

    std::vector<cwmode::ModeSpec> modes;
    for (long i = 0; i < count; ++i) {
        cwmode::ModeSpec m;
        std::printf("[ * ] Code for mode %ld:\n", i);
        uint32_t code = 0;
        if (!parse_hex(ask(""), code) || code > 0xFF) {
            std::fprintf(stderr, "cwmode: error: invalid mode code\n");
            return 1;
        }
        m.code = (uint8_t)code;

        std::printf("[ * ] Icon for mode %ld:\n", i);
        std::string error;
        if (!cwmode::load_image(ask(""), m.icon, error)) {
            std::fprintf(stderr, "cwmode: error: %s\n", error.c_str());
            return 1;
        }
        for (int j = 0; j < 2; ++j) {
            if (a.model == cwmode::Model::Cw580) {
                std::printf("[ * ] Name for mode %ld (language %d):\n", i, j);
                m.names.push_back(ask(""));
            } else {
                std::printf("[ * ] Text image for mode %ld (language %d):\n", i, j);
                cwmode::Image image;
                if (!cwmode::load_image(ask(""), image, error)) {
                    std::fprintf(stderr, "cwmode: error: %s\n", error.c_str());
                    return 1;
                }
                m.text_images.push_back(std::move(image));
            }
        }
        std::printf("[ * ] Entry addr for mode %ld:\n", i);
        uint32_t entry = 0;
        if (!parse_hex(ask(""), entry)) {
            std::fprintf(stderr, "cwmode: error: invalid entry address\n");
            return 1;
        }
        m.entry = entry;
        modes.push_back(std::move(m));
    }

    for (size_t i = 0; i < modes.size(); ++i)
        if (modes[i].entry == 0)
            std::fprintf(stderr,
                         "cwmode: warning: mode %zu has entry 0; selecting it jumps to the "
                         "reset vector\n",
                         i);

    cwmode::PatchResult result = cwmode::patch(rom, modes, options);
    if (!result.ok) {
        std::fprintf(stderr, "cwmode: error: %s\n", result.error.c_str());
        return 1;
    }
    if (verbose)
        for (const std::string& line : result.log) std::printf("%s\n", line.c_str());
    if (!write_binary_file(out_path, result.rom)) {
        std::fprintf(stderr, "cwmode: error: cannot write %s\n", out_path.c_str());
        return 1;
    }
    std::printf("[ + ] Done\n");
    std::printf("    %u modes -> %u, %u bytes at %s, %u free\n", result.analysis.mode_count,
                result.mode_count_after, result.final_cursor - result.target,
                hex_addr(result.target).c_str(), result.analysis.window - result.final_cursor);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> positional;
    std::string model_text, out_path;
    bool interactive = false, force = false, info = false, json = false, verbose = false;
    bool have_model = false, have_modes = false;
    long declared_modes = -1;
    uint32_t at = 0;
    cwmode::Options options;
    std::vector<cwmode::ModeSpec> modes;
    int current = -1;  // index into modes

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "cwmode: error: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (arg == "--model") {
            model_text = value("--model");
            have_model = true;
        } else if (arg == "--modes") {
            const std::string text = value("--modes");
            if (!parse_dec(text, declared_modes) || declared_modes < 1 || declared_modes > 64) {
                std::fprintf(stderr, "cwmode: error: --modes must be 1..64\n");
                return 2;
            }
            have_modes = true;
        } else if (arg == "--code") {
            uint32_t code = 0;
            if (!parse_hex(value("--code"), code) || code > 0xFF) {
                std::fprintf(stderr, "cwmode: error: --code must be a byte in hex\n");
                return 2;
            }
            cwmode::ModeSpec m;
            m.code = (uint8_t)code;
            modes.push_back(std::move(m));
            current = (int)modes.size() - 1;
        } else if (arg == "--icon") {
            if (current < 0) {
                std::fprintf(stderr, "cwmode: error: --icon before --code\n");
                return 2;
            }
            std::string error;
            if (!cwmode::load_image(value("--icon"), modes[(size_t)current].icon, error)) {
                std::fprintf(stderr, "cwmode: error: %s\n", error.c_str());
                return 2;
            }
        } else if (arg == "--name" || arg == "--name-image") {
            if (current < 0) {
                std::fprintf(stderr, "cwmode: error: --name before --code\n");
                return 2;
            }
            const std::string text = value("--name");
            if (!have_model) {
                std::fprintf(stderr, "cwmode: error: --model must come before --name\n");
                return 2;
            }
            cwmode::Model model = cwmode::Model::Cw580;
            cwmode::parse_model(model_text, model);
            if (model == cwmode::Model::Cw580) {
                if (arg == "--name-image") {
                    std::fprintf(stderr, "cwmode: error: the 580 uses text names (--name)\n");
                    return 2;
                }
                modes[(size_t)current].names.push_back(text);
            } else {
                std::string error;
                cwmode::Image image;
                if (!cwmode::load_image(text, image, error)) {
                    std::fprintf(stderr,
                                 "cwmode: error: %s (the 880 uses 64x13 text images for --name)\n",
                                 error.c_str());
                    return 2;
                }
                modes[(size_t)current].text_images.push_back(std::move(image));
            }
        } else if (arg == "--entry") {
            uint32_t entry = 0;
            if (!parse_hex(value("--entry"), entry)) {
                std::fprintf(stderr, "cwmode: error: --entry must be a hex address\n");
                return 2;
            }
            if (current < 0) {
                std::fprintf(stderr, "cwmode: error: --entry before --code\n");
                return 2;
            }
            modes[(size_t)current].entry = entry;
        } else if (arg == "--at") {
            if (!parse_hex(value("--at"), at)) {
                std::fprintf(stderr, "cwmode: error: --at must be a hex address\n");
                return 2;
            }
        } else if (arg == "--force") {
            force = true;
        } else if (arg == "--interactive") {
            interactive = true;
        } else if (arg == "--info") {
            info = true;
        } else if (arg == "--json") {
            json = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg.size() > 1 && arg[0] == '-') {
            std::fprintf(stderr, "cwmode: error: unknown option %s\n", arg.c_str());
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (!have_model || !cwmode::parse_model(model_text, options.model)) {
        std::fprintf(stderr, "cwmode: error: --model 580|880 is required\n");
        std::fputs("Try --help.\n", stderr);
        return 2;
    }
    options.force = force;
    options.at = at;

    if (positional.empty()) {
        std::fprintf(stderr, "cwmode: error: missing input ROM\n");
        std::fputs("Try --help.\n", stderr);
        return 2;
    }
    const std::string rom_path = positional[0];
    bool ok = false;
    std::vector<uint8_t> rom = read_binary_file(rom_path, &ok);
    if (!ok) {
        std::fprintf(stderr, "cwmode: error: cannot read %s\n", rom_path.c_str());
        return 1;
    }
    cwmode::Analysis analysis = cwmode::analyze(rom, options.model);
    if (!analysis.ok) {
        std::fprintf(stderr, "cwmode: error: %s\n", analysis.error.c_str());
        return 1;
    }

    if (info) {
        print_info(analysis);
        return 0;
    }

    if (positional.size() > 1) out_path = positional[1];
    if (out_path.empty()) {
        std::fprintf(stderr, "cwmode: error: missing output ROM\n");
        return 2;
    }
    {
        std::error_code ec1, ec2;
        const fs::path a = fs::weakly_canonical(fs::path(rom_path), ec1);
        const fs::path b = fs::weakly_canonical(fs::path(out_path), ec2);
        if (!ec1 && !ec2 && a == b) {
            std::fprintf(stderr, "cwmode: error: refusing to overwrite the input ROM\n");
            return 2;
        }
    }

    if (interactive) return run_interactive(rom, analysis, options, out_path, verbose);

    if (!have_modes) {
        std::fprintf(stderr, "cwmode: error: --modes N is required (or use --interactive)\n");
        return 2;
    }
    if ((long)modes.size() != declared_modes) {
        std::fprintf(stderr, "cwmode: error: --modes %ld but %zu mode(s) defined\n", declared_modes,
                     modes.size());
        return 2;
    }

    for (size_t i = 0; i < modes.size(); ++i)
        if (modes[i].entry == 0)
            std::fprintf(stderr,
                         "cwmode: warning: mode %zu has entry 0; selecting it jumps to the "
                         "reset vector\n",
                         i);

    cwmode::PatchResult result = cwmode::patch(rom, modes, options);
    if (!result.ok) {
        std::fprintf(stderr, "cwmode: error: %s\n", result.error.c_str());
        return 1;
    }
    if (verbose)
        for (const std::string& line : result.log) std::printf("%s\n", line.c_str());

    if (!write_binary_file(out_path, result.rom)) {
        std::fprintf(stderr, "cwmode: error: cannot write %s\n", out_path.c_str());
        return 1;
    }

    if (json) {
        std::printf(
            "{\"model\":\"%s\",\"mode_count\":%u,\"mode_count_before\":%u,\"target\":%u,"
            "\"final_cursor\":%u,\"icon_table\":%u,\"str_table\":%u,\"changed_spans\":%zu}\n",
            cwmode::model_name(options.model), result.mode_count_after, result.analysis.mode_count,
            result.target, result.final_cursor, result.icon_table, result.str_table,
            result.changed.size());
    } else {
        std::printf("cwmode: patched %s -> %s\n", rom_path.c_str(), out_path.c_str());
        std::printf("    %u modes -> %u, %u bytes at %s, %u bytes free\n",
                    result.analysis.mode_count, result.mode_count_after,
                    result.final_cursor - result.target, hex_addr(result.target).c_str(),
                    result.analysis.window - result.final_cursor);
        for (const auto& span : result.changed)
            std::printf("    changed %04X..%04X (%u bytes)\n", span.first, span.second,
                        span.second - span.first);
    }
    return 0;
}
