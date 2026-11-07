// test_preserve.cpp - cwapp #preserve / preserve() / restore_preserved().
#include <cstdio>
#include <string>
#include <vector>

#include "build.hpp"
#include "cwiz.hpp"
#include "harness.hpp"
#include "sim.hpp"

using namespace wt;

namespace {

struct Run {
    wiz::U8 m;
    std::string why;
    wiz::BuildResult br;
};

Run run_app(const std::string& src, int addr = -1, int value = -1, int more_addr = -1,
            int more_value = -1, long steps = 2000000) {
    wiz::BuildResult br = wiz::build_cwiz(src, "cwapp");
    const int tramp = br.syms.at("_TRAMP");
    wiz::U8 m(br.rom, {}, "cwapp", tramp >> 16, tramp & 0xFFFF, {{2, 0xAD4C}});
    m.r[8] = 0xE0;
    if (addr >= 0) m.mem[addr] = (uint8_t)value;
    if (more_addr >= 0) m.mem[more_addr] = (uint8_t)more_value;
    std::string why = m.run(steps);
    return Run{std::move(m), why, std::move(br)};
}

bool compile_fails(const std::string& src, const std::string& needle, std::string* got = nullptr) {
    try {
        wiz::build_cwiz(src, "cwapp");
    } catch (const std::exception& e) {
        if (got) *got = e.what();
        return std::string(e.what()).find(needle) != std::string::npos;
    }
    return false;
}

const char* kHeader = "#for 580\n#cwapp\n#slot 1\n";

}  // namespace

void wt::run_preserve() {
    // One range: mutate it in main, exit, restore the exact byte.
    {
        const std::string src = std::string(kHeader) +
            "#preserve 0xD200 1\n"
            "void main() { poke(0xD200, 0x42); delay(2); exit(); }\n";
        Run r = run_app(src, 0xD200, 0x5A);
        check("preserve: one range restores the byte on exit",
              r.why == "stop at 2:AD4C" && r.m.mem[0xD200] == 0x5A, r.why);
        const int st = r.br.syms.at("__cwapp_preserve_state");
        check("preserve: one descriptor is registered",
              r.m.mem[st] == 1 && r.m.mem[st + 1] == 1 &&
                  (r.m.mem[st + 2] | (r.m.mem[st + 3] << 8)) == 0xD200 &&
                  (r.m.mem[st + 4] | (r.m.mem[st + 5] << 8)) == 1);
    }

    // Multiple ranges.
    {
        const std::string src = std::string(kHeader) +
            "#preserve 0xD200 1\n"
            "#preserve 0xE000 4\n"
            "void main() { poke(0xD200, 0x11); poke(0xE000, 0x22); "
            "poke(0xE003, 0x33); delay(2); exit(); }\n";
        Run r = run_app(src, 0xD200, 0xA5, 0xE000, 0x5A);
        check("preserve: multiple ranges restore byte-exactly",
              r.why == "stop at 2:AD4C" && r.m.mem[0xD200] == 0xA5 &&
                  r.m.mem[0xE000] == 0x5A && r.m.mem[0xE003] == 0xFF,
              r.why);
        const int st = r.br.syms.at("__cwapp_preserve_state");
        check("preserve: two descriptors and five snapshot bytes",
              r.m.mem[st] == 2 && r.m.mem[st + 1] == 5);
    }

    // Mid-run restore: after restore_preserved() the original value is back.
    {
        const std::string src = std::string(kHeader) +
            "#preserve 0xD200 1\n"
            "void main() { poke(0xD200, 0x42); restore_preserved(); "
            "while (1) { delay(1); } }\n";
        Run r = run_app(src, 0xD200, 0x5A, -1, -1, 300000);
        check("preserve: restore_preserved() restores in the middle of a run",
              r.m.mem[0xD200] == 0x5A, r.why);
    }

    // Dynamic registration.
    {
        const std::string src = std::string(kHeader) +
            "int ok = 0;\n"
            "void main() { ok = preserve(0xD200, 1); poke(0xD200, 0x42); "
            "delay(2); exit(); }\n";
        Run r = run_app(src, 0xD200, 0x5A);
        check("preserve: preserve(addr, len) registers and restores on exit",
              r.why == "stop at 2:AD4C" && r.m.mem[0xD200] == 0x5A &&
                  uvar_of(r.m, r.br.variables, "ok") == 1,
              r.why);
    }

    // Runtime rejection: outside RAM, save area, and capacity overflow.
    {
        const std::string src = std::string(kHeader) +
            "int a = 0, b = 0, c = 0;\n"
            "void main() { a = preserve(0x0100, 1); b = preserve(0xE5D3, 1);\n"
            "  preserve(0xD300,1); preserve(0xD301,1); preserve(0xD302,1);\n"
            "  preserve(0xD303,1); preserve(0xD304,1); preserve(0xD305,1);\n"
            "  preserve(0xD306,1); preserve(0xD307,1); c = preserve(0xD308,1);\n"
            "  delay(2); exit(); }\n";
        Run r = run_app(src);
        check("preserve: preserve() rejects bad addresses and a full table",
              r.why == "stop at 2:AD4C" && uvar_of(r.m, r.br.variables, "a") == 0 &&
                  uvar_of(r.m, r.br.variables, "b") == 0 &&
                  uvar_of(r.m, r.br.variables, "c") == 0,
              r.why);
    }

    // Compile-time validation diagnostics.
    {
        std::string got;
        check("preserve: address below RAM is rejected",
              compile_fails(std::string(kHeader) + "#preserve 0xCFFF 1\nvoid main() { }\n",
                            "outside cwapp RAM", &got), got);
        check("preserve: range crossing the top of RAM is rejected",
              compile_fails(std::string(kHeader) + "#preserve 0xFFFF 2\nvoid main() { }\n",
                            "outside cwapp RAM", &got), got);
        check("preserve: the saved stack area is rejected",
              compile_fails(std::string(kHeader) + "#preserve 0xE5D3 1\nvoid main() { }\n",
                            "save area", &got), got);
        check("preserve: overlapping directive ranges are rejected",
              compile_fails(std::string(kHeader) + "#preserve 0xD200 4\n"
                                                   "#preserve 0xD202 1\nvoid main() { }\n",
                            "overlaps", &got), got);
        std::string nine = std::string(kHeader);
        for (int i = 0; i < 9; i++) nine += wiz::fmt("#preserve 0xD%03X 1\n", 0x200 + i);
        nine += "void main() { }\n";
        check("preserve: more than 8 ranges is a compile error",
              compile_fails(nine, "at most 8", &got), got);
        check("preserve: more than 128 snapshot bytes is a compile error",
              compile_fails(std::string(kHeader) + "#preserve 0xD200 100\n"
                                                   "#preserve 0xD300 40\nvoid main() { }\n",
                            "at most 128", &got), got);
    }

    // The hidden state block is allocated by the RAM allocator, never fixed.
    {
        const std::string a = std::string(kHeader) + "#preserve 0xD200 1\n"
                              "int x;\nvoid main() { x = 1; }\n";
        const std::string b = std::string(kHeader) + "#preserve 0xD300 2\n"
                              "int x;\nvoid main() { x = 1; }\n";
        wiz::BuildResult ba = wiz::build_cwiz(a, "cwapp");
        wiz::BuildResult bb = wiz::build_cwiz(b, "cwapp");
        const int sa = ba.syms.at("__cwapp_preserve_state");
        const int sb = bb.syms.at("__cwapp_preserve_state");
        check("preserve: the state block lives in allocated cwapp RAM",
              sa >= 0xD000 && sa < 0xDDD3 && sb >= 0xD000 && sb < 0xDDD3);
    }
}
