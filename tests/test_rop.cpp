#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "harness.hpp"
#include "rop.hpp"

using namespace wt;

namespace {

constexpr size_t kNoPos = static_cast<size_t>(-1);

bool has16(const std::vector<uint8_t>& bytes, uint32_t value) {
    const uint8_t lo = static_cast<uint8_t>(value & 0xFF);
    const uint8_t hi = static_cast<uint8_t>((value >> 8) & 0xFF);
    for (size_t i = 0; i + 1 < bytes.size(); ++i)
        if (bytes[i] == lo && bytes[i + 1] == hi) return true;
    return false;
}

size_t pool_offset(const std::vector<uint8_t>& bytes, const std::string& text) {
    auto it = std::search(bytes.begin(), bytes.end(), text.begin(), text.end());
    return it == bytes.end() ? bytes.size() : static_cast<size_t>(it - bytes.begin());
}

// ---------------------------------------------------------------- byte helpers
std::vector<uint8_t> seq(std::initializer_list<int> xs) {
    std::vector<uint8_t> v;
    for (int x : xs) v.push_back(static_cast<uint8_t>(x));
    return v;
}

// Offset of `needle` inside `hay`, kNoPos when it is not there.
size_t find_seq(const std::vector<uint8_t>& hay, const std::vector<uint8_t>& needle) {
    if (needle.empty() || needle.size() > hay.size()) return kNoPos;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (std::equal(needle.begin(), needle.end(), hay.begin() + i)) return i;
    return kNoPos;
}

bool has_seq(const std::vector<uint8_t>& hay, const std::vector<uint8_t>& needle) {
    return find_seq(hay, needle) != kNoPos;
}

// `count` bytes starting at `pos`, or an empty vector.
std::vector<uint8_t> at(const std::vector<uint8_t>& bytes, size_t pos, size_t count) {
    if (pos == kNoPos || pos + count > bytes.size()) return {};
    return std::vector<uint8_t>(bytes.begin() + static_cast<long>(pos),
                                bytes.begin() + static_cast<long>(pos + count));
}

std::string hex_of(const std::vector<uint8_t>& bytes) {
    std::string s;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i) s += ' ';
        s += wiz::fmt("%02X", bytes[i]);
    }
    return s;
}

// The nX-U16 chain word of a gadget: [addr_lo, addr_hi, 0x30 | segment, variant].
std::vector<uint8_t> word(uint32_t addr, uint8_t variant = 0x30) {
    return seq({static_cast<int>(addr & 0xFF), static_cast<int>((addr >> 8) & 0xFF),
                static_cast<int>(0x30 | ((addr >> 16) & 0x0F)), static_cast<int>(variant)});
}

// The four bytes that follow a gadget word at `pos`.
std::vector<uint8_t> args_after(const std::vector<uint8_t>& bytes, const std::vector<uint8_t>& w) {
    return at(bytes, find_seq(bytes, w) + w.size(), 4);
}

const char* hello_source() {
    return "#rop\n"
           "#launcher common\n"
           "int main() {\n"
           "    print(0x30, 0x01, \"ROP ADDRESS\");\n"
           "    render();\n"
           "}\n";
}

// The 580 "launcher chung" of the community, byte for byte.
//   FD 24 | 30 30 | DA 7B 31 30 | FE <code> | <src> | <dst> | <dst-2> |
//   32 89 31 30 | 30 30 | 74 1F 32 48
std::vector<uint8_t> community_launcher(uint8_t code, uint16_t src, uint16_t dst) {
    std::vector<uint8_t> v;
    const uint8_t s[26] = {
        0xFD, 0x24, 0x30, 0x30, 0xDA, 0x7B, 0x31, 0x30, 0xFE,
        0x00,                                                   // code
        0x00, 0x00,                                             // source
        0x00, 0x00,                                             // destination
        0x00, 0x00,                                             // jump target (dst - 2)
        0x32, 0x89, 0x31, 0x30, 0x30, 0x30, 0x74, 0x1F, 0x32, 0x48};
    v.assign(s, s + 26);
    v[9] = code;
    v[10] = static_cast<uint8_t>(src & 0xFF);
    v[11] = static_cast<uint8_t>((src >> 8) & 0xFF);
    v[12] = static_cast<uint8_t>(dst & 0xFF);
    v[13] = static_cast<uint8_t>((dst >> 8) & 0xFF);
    v[14] = static_cast<uint8_t>((dst - 2) & 0xFF);
    v[15] = static_cast<uint8_t>(((dst - 2) >> 8) & 0xFF);
    return v;
}

}  // namespace

void wt::run_rop() {
    check("rop: source marker selects backend", wiz::source_is_rop(hello_source()));

    // Cross-compiled with RAC/hdcompiler's equivalent low-level source:
    // The compiler supplies RAC's setlr; setsfr preamble, then emits
    // buffer_clear; pop xr0 (linepos, pad, adr); printline; render.ddd4.
    const auto rac_equivalent = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { screen_del(); print(0x30, 0x01, \"HELLO ROP\"); "
        "render(); }\n", "580");
    std::vector<uint8_t> rac_bytes = {
        0xA8, 0x9F, 0x30, 0x30, 0xE0, 0xA0, 0x30, 0x30,
        0x60, 0x8C, 0x30, 0x30,
        0x34, 0x7B, 0x31, 0x30, 0x01, 0x30, 0x00, 0xEA,
        0xC8, 0x3D, 0x32, 0x30, 0x7E, 0x94, 0x30, 0x30,
        0x30, 0x30, 0x33, 0x30,
        0x48, 0x45, 0x4C, 0x4C,
        0x4F, 0x20, 0x52, 0x4F, 0x50, 0x00,
        // 256-byte null buffer appended by default
    };
    for (int i = 0; i < 256; ++i) rac_bytes.push_back(0x00);
    check("rop: Wiz/RAC equivalent emits identical 580 bytes",
          rac_equivalent.ok && rac_equivalent.bytes == rac_bytes,
          hex_of(rac_equivalent.bytes));
    const auto legacy_setup = wiz::build_rop(
        "#rop\n#org E9E0\nint main() { setlr_pc(); setsfr(); screen_del(); }\n", "580");
    check("rop: legacy setlr_pc spelling uses RAC setlr", legacy_setup.ok &&
          legacy_setup.bytes.size() >= 4 && legacy_setup.bytes[0] == 0xA8 &&
          legacy_setup.bytes[1] == 0x9F);
    // The community launcher, exactly as it is typed into the calculator.
    const std::vector<uint8_t> rac_launcher = community_launcher(0x02, 0xE9E0, 0xD730);
    check("rop: 580 common launcher matches RAC", rac_equivalent.launcher == rac_launcher,
          hex_of(rac_equivalent.launcher));
    check("rop: 580 common launcher is 26 bytes", rac_equivalent.launcher.size() == 26,
          wiz::fmt("%zu", rac_equivalent.launcher.size()));
    check("rop: 580 common launcher ends with the flow gadget",
          has_seq(rac_equivalent.launcher, seq({0x74, 0x1F, 0x32, 0x48})));

    // The length codes of the community launcher: 01 -> 0x01FE, 02 -> 0x02FE,
    // 03 -> 0x03FE bytes.
    const auto l_short = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common short\n"
                                         "int main() { render(); }\n", "580");
    const auto l_medium = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common medium\n"
                                          "int main() { render(); }\n", "580");
    const auto l_long = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common long\n"
                                        "int main() { render(); }\n", "580");
    check("rop: 580 common short launcher uses code 01",
          l_short.launcher == community_launcher(0x01, 0xE9E0, 0xD730),
          hex_of(l_short.launcher));
    check("rop: 580 common medium launcher uses code 02",
          l_medium.launcher == community_launcher(0x02, 0xE9E0, 0xD730),
          hex_of(l_medium.launcher));
    check("rop: 580 common long launcher uses code 03",
          l_long.launcher == community_launcher(0x03, 0xE9E0, 0xD730),
          hex_of(l_long.launcher));
    check("rop: 580 common launcher length codes only change the code byte",
          l_short.launcher.size() == 26 && l_medium.launcher.size() == 26 &&
              l_long.launcher.size() == 26 && l_short.launcher[9] == 0x01 &&
              l_medium.launcher[9] == 0x02 && l_long.launcher[9] == 0x03);

    const auto r580 = wiz::build_rop(hello_source(), "580");
    const size_t p580 = pool_offset(r580.bytes, "ROP ADDRESS");
    check("rop: 580 common build succeeds", r580.ok && !r580.bytes.empty());
    check("rop: 580 payload stays at injection address", r580.base == 0xE9E0);
    check("rop: 580 common launcher uses input area", r580.launcher_base == 0xD180);
    check("rop: 580 main payload uses RAC call tags",
          r580.bytes.size() >= 4 && r580.bytes[2] == 0x30 && r580.bytes[3] == 0x30,
          r580.bytes.size() >= 4 ? wiz::fmt("%02X %02X %02X %02X", r580.bytes[0], r580.bytes[1],
                                            r580.bytes[2], r580.bytes[3]) : "short");
    check("rop: 580 pointers target backup payload", p580 < r580.bytes.size() &&
                                                       has16(r580.bytes, 0xE9E0 + p580));
    check("rop: 580 pointers do not target copied execution address",
          p580 >= r580.bytes.size() || !has16(r580.bytes, 0xD730 + p580));

    // ---------------------------------------------------------------- segments
    // The third byte of a chain word carries the code segment of the gadget
    // address (0x30 | segment): printline lives at 0x23DC8, which is segment 2,
    // so it is called as "C8 3D 32 30".
    check("rop: printline carries the address segment",
          has_seq(rac_equivalent.bytes, seq({0xC8, 0x3D, 0x32, 0x30})) &&
              !has_seq(rac_equivalent.bytes, seq({0xC8, 0x3D, 0x30, 0x30})));
    check("rop: pop xr0 carries the address segment",
          has_seq(rac_equivalent.bytes, seq({0x34, 0x7B, 0x31, 0x30})));
    check("rop: setlr carries the address segment",
          has_seq(rac_equivalent.bytes, seq({0xA8, 0x9F, 0x30, 0x30})));
    check("rop: gadget at 0x33030 carries segment 3",
          has_seq(rac_equivalent.bytes, word(0x33030)) &&
              at(rac_equivalent.bytes, 28, 4) == seq({0x30, 0x30, 0x33, 0x30}),
          hex_of(at(rac_equivalent.bytes, 28, 4)));
    check("rop: chain words follow the community encoding",
          has_seq(rac_equivalent.bytes, word(0x09FA8)) &&
              has_seq(rac_equivalent.bytes, word(0x17B34)) &&
              has_seq(rac_equivalent.bytes, word(0x23DC8)) &&
              has_seq(rac_equivalent.bytes, word(0x0947E)) &&
              has_seq(rac_equivalent.bytes, word(0x33030)));

    // ---------------------------------------------------------------- print
    // print(pad, linepos, "text") -> pop xr0 [linepos, pad, adr_lo, adr_hi]
    const auto r_print = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                        "int main() { print(0x30, 0x21, \"PRINT\"); }\n", "580");
    const size_t pp = pool_offset(r_print.bytes, "PRINT");
    check("rop: print emits pop xr0", has_seq(r_print.bytes, seq({0x34, 0x7B, 0x31, 0x30})));
    check("rop: print argument block is [linepos, pad, adr_lo, adr_hi]",
          pp < r_print.bytes.size() &&
              args_after(r_print.bytes, seq({0x34, 0x7B, 0x31, 0x30})) ==
                  seq({0x21, 0x30, static_cast<int>((0xE9E0 + pp) & 0xFF),
                       static_cast<int>(((0xE9E0 + pp) >> 8) & 0xFF)}),
          hex_of(args_after(r_print.bytes, seq({0x34, 0x7B, 0x31, 0x30}))));
    check("rop: print calls printline", has_seq(r_print.bytes, seq({0xC8, 0x3D, 0x32, 0x30})));

    // The string may also come first: print("text", pad, linepos).
    const auto r_print2 = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                         "int main() { print(\"PRINT2\", 0x30, 0x21); }\n", "580");
    const size_t pp2 = pool_offset(r_print2.bytes, "PRINT2");
    check("rop: print accepts the text first",
          pp2 < r_print2.bytes.size() &&
              args_after(r_print2.bytes, seq({0x34, 0x7B, 0x31, 0x30})) ==
                  seq({0x21, 0x30, static_cast<int>((0xE9E0 + pp2) & 0xFF),
                       static_cast<int>(((0xE9E0 + pp2) >> 8) & 0xFF)}),
          hex_of(args_after(r_print2.bytes, seq({0x34, 0x7B, 0x31, 0x30}))));

    // ---------------------------------------------------------------- smallprint
    // print_small(font, linepos, "text") -> pop xr0 [font, linepos, adr]
    const auto r_small = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                         "int main() { print_small(0x0E, 0x11, \"SMALL\"); }\n", "580");
    const size_t ps = pool_offset(r_small.bytes, "SMALL");
    check("rop: print_small calls smallprint", has_seq(r_small.bytes, seq({0xCC, 0x3D, 0x32, 0x30})));
    check("rop: print_small argument block is [font, linepos, adr_lo, adr_hi]",
          ps < r_small.bytes.size() &&
              args_after(r_small.bytes, seq({0x34, 0x7B, 0x31, 0x30})) ==
                  seq({0x0E, 0x11, static_cast<int>((0xE9E0 + ps) & 0xFF),
                       static_cast<int>(((0xE9E0 + ps) >> 8) & 0xFF)}),
          hex_of(args_after(r_small.bytes, seq({0x34, 0x7B, 0x31, 0x30}))));
    const auto r_small2 = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                          "int main() { print_small(\"SMALL2\", 0x0E, 0x11); }\n", "580");
    const size_t ps2 = pool_offset(r_small2.bytes, "SMALL2");
    check("rop: print_small accepts the text first",
          ps2 < r_small2.bytes.size() &&
              args_after(r_small2.bytes, seq({0x34, 0x7B, 0x31, 0x30})) ==
                  seq({0x0E, 0x11, static_cast<int>((0xE9E0 + ps2) & 0xFF),
                       static_cast<int>(((0xE9E0 + ps2) >> 8) & 0xFF)}));

    // ---------------------------------------------------------------- line/rect
    // line(x1, y1, x2, y2) -> pop xr0 [x1, y1, x2, y2], then line_draw.
    const auto r_line = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                        "int main() { line(0x00, 0x10, 0xC0, 0x3F); }\n", "580");
    check("rop: line argument block is [x1, y1, x2, y2]",
          args_after(r_line.bytes, seq({0x34, 0x7B, 0x31, 0x30})) ==
              seq({0x00, 0x10, 0xC0, 0x3F}),
          hex_of(args_after(r_line.bytes, seq({0x34, 0x7B, 0x31, 0x30}))));
    check("rop: line calls line_draw", has_seq(r_line.bytes, seq({0x62, 0x8E, 0x30, 0x30})));
    // The argument block comes before the call, never after it.
    {
        const size_t pop = find_seq(r_line.bytes, seq({0x34, 0x7B, 0x31, 0x30}));
        const size_t call = find_seq(r_line.bytes, seq({0x62, 0x8E, 0x30, 0x30}));
        check("rop: line arguments precede the call", pop != kNoPos && call != kNoPos &&
                                                       call == pop + 8,
              wiz::fmt("pop=%zu call=%zu", pop, call));
    }
    // rect(x, y, w, h) draws four line_draw edges: top, bottom, left, right.
    const auto r_rect = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                        "int main() { rect(0x05, 0x06, 0x07, 0x08); }\n", "580");
    {
        const std::vector<uint8_t> call = word(0x08E62);
        size_t count = 0;
        for (size_t i = 0; i + call.size() <= r_rect.bytes.size(); ++i)
            if (std::equal(call.begin(), call.end(), r_rect.bytes.begin() + (long)i)) ++count;
        check("rop: rect draws four line_draw edges", r_rect.ok && count == 4,
              wiz::fmt("%zu line_draw calls", count));
        check("rop: rect uses x+w and y+h for the far edges",
              has_seq(r_rect.bytes, seq({0x05, 0x06, 0x0C, 0x06})) &&
                  has_seq(r_rect.bytes, seq({0x05, 0x0E, 0x0C, 0x0E})) &&
                  has_seq(r_rect.bytes, seq({0x05, 0x06, 0x05, 0x0E})) &&
                  has_seq(r_rect.bytes, seq({0x0C, 0x06, 0x0C, 0x0E})),
              hex_of(r_rect.bytes));
    }

    // ---------------------------------------------------------------- bitmap
    // bitmap(x, y, w, h, "ff81...") -> pop xr0 [x, y, w, h], render_bitmap,
    // pop er0 with the bitmap address, then render.
    const auto r_bitmap = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { bitmap(0x0A, 0x0A, 0x08, 0x08, \"ff818181818181ff\"); }\n", "580");
    check("rop: bitmap argument block is [x, y, w, h]",
          args_after(r_bitmap.bytes, seq({0x34, 0x7B, 0x31, 0x30})) ==
              seq({0x0A, 0x0A, 0x08, 0x08}),
          hex_of(args_after(r_bitmap.bytes, seq({0x34, 0x7B, 0x31, 0x30}))));
    check("rop: bitmap calls render_bitmap", has_seq(r_bitmap.bytes, seq({0x48, 0x98, 0x30, 0x30})));
    check("rop: bitmap loads the address with pop er0",
          has_seq(r_bitmap.bytes, seq({0x02, 0x26, 0x31, 0x30})));
    check("rop: bitmap calls render afterwards", has_seq(r_bitmap.bytes, seq({0x7E, 0x94, 0x30, 0x30})));
    {
        const size_t pop = find_seq(r_bitmap.bytes, seq({0x34, 0x7B, 0x31, 0x30}));
        const size_t rb = find_seq(r_bitmap.bytes, seq({0x48, 0x98, 0x30, 0x30}));
        const size_t pe = find_seq(r_bitmap.bytes, seq({0x02, 0x26, 0x31, 0x30}));
        const size_t rn = find_seq(r_bitmap.bytes, seq({0x7E, 0x94, 0x30, 0x30}));
        check("rop: bitmap chain order is pop/args, render_bitmap, pop er0, render",
              pop != kNoPos && rb == pop + 8 && pe == rb + 4 && rn == pe + 6,
              wiz::fmt("pop=%zu render_bitmap=%zu pop_er0=%zu render=%zu", pop, rb, pe, rn));
    }
    // Space separated hex bytes are accepted as well.
    const auto r_bitmap2 = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { bitmap(1, 2, 8, 8, \"ff 81 81 ff\"); }\n", "580");
    check("rop: bitmap accepts space separated hex bytes",
          has_seq(r_bitmap2.bytes, seq({0xFF, 0x81, 0x81, 0xFF})));

    // ---------------------------------------------------------------- delay / pixel
    const auto r_delay = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                         "int main() { delay(0x1F40); }\n", "580");
    {
        const std::vector<uint8_t> pop = word(0x12602);   // pop er0
        const size_t p = find_seq(r_delay.bytes, pop);
        check("rop: delay loads er0 before the delay gadget",
              p != kNoPos && at(r_delay.bytes, p + 4, 2) == seq({0x40, 0x1F}) &&
                  at(r_delay.bytes, p + 6, 4) == word(0x09F3C),
              hex_of(r_delay.bytes));
    }
    const auto r_pixel = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                         "int main() { pixel(0x0A, 0x14, 1); }\n", "580");
    {
        const std::vector<uint8_t> pop = word(0x17B34);   // pop xr0
        const size_t p = find_seq(r_pixel.bytes, pop);
        check("rop: pixel argument block is [x, y, color, pad]",
              p != kNoPos && at(r_pixel.bytes, p + 4, 4) == seq({0x0A, 0x14, 0x01, 0x00}),
              hex_of(r_pixel.bytes));
        check("rop: pixel calls pixel_draw",
              has_seq(r_pixel.bytes, word(0x091FC)));
    }
    const auto r_shift = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n"
                                         "int main() { waitshift(); }\n", "580");
    check("rop: waitshift calls the waitshift gadget",
          has_seq(r_shift.bytes, word(0x23DDE)), hex_of(r_shift.bytes));

    // ---------------------------------------------------------------- number helpers
    const auto r_calc = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { calc_func(0xD947, 0xD92E); }\n", "580");
    {
        const size_t p = find_seq(r_calc.bytes, word(0x17B34));
        check("rop: calc_func passes [ptr, result] in xr0",
              p != kNoPos && at(r_calc.bytes, p + 4, 4) == seq({0x47, 0xD9, 0x2E, 0xD9}) &&
                  has_seq(r_calc.bytes, word(0x17922)),
              hex_of(r_calc.bytes));
    }
    const auto r_nts = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { num_to_str(0xD92E, 0xD947); }\n", "580");
    {
        const size_t p = find_seq(r_nts.bytes, word(0x17B34));
        check("rop: num_to_str passes [number, text] in xr0",
              p != kNoPos && at(r_nts.bytes, p + 4, 4) == seq({0x2E, 0xD9, 0x47, 0xD9}) &&
                  has_seq(r_nts.bytes, word(0x279B6)),
              hex_of(r_nts.bytes));
    }

    // ---------------------------------------------------------------- memory
    const auto r_memcpy = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { memcpy(0xD630, 0xD184, 0x02FE); }\n", "580");
    {
        const std::vector<uint8_t> pop = word(0x12DA8);   // pop qr8
        const size_t p = find_seq(r_memcpy.bytes, pop);
        check("rop: memcpy argument block is [dst, src, length, pad]",
              p != kNoPos && at(r_memcpy.bytes, p + 4, 8) ==
                                 seq({0x30, 0xD6, 0x84, 0xD1, 0xFE, 0x02, 0x00, 0x00}),
              hex_of(r_memcpy.bytes));
        check("rop: memcpy calls memcpy_length_rn/8",
              has_seq(r_memcpy.bytes, word(0x10F20)));
    }
    const auto r_strcpy = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { strcpy(0xD630, 0xD184); }\n", "580");
    {
        const std::vector<uint8_t> pop = word(0x17B34);   // pop xr0
        const size_t p = find_seq(r_strcpy.bytes, pop);
        check("rop: strcpy argument block is [dst, src]",
              p != kNoPos && at(r_strcpy.bytes, p + 4, 4) == seq({0x30, 0xD6, 0x84, 0xD1}),
              hex_of(r_strcpy.bytes));
        check("rop: strcpy calls BL strcpy", has_seq(r_strcpy.bytes, word(0x203CC)));
    }
    const auto r_strcat = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { str_cat(0xD100, 0xD120); }\n", "580");
    {
        const size_t p = find_seq(r_strcat.bytes, word(0x17B34));
        check("rop: str_cat loads destination/source and calls the BL gadget",
              r_strcat.ok && p != kNoPos && at(r_strcat.bytes, p + 4, 4) ==
                  seq({0x00, 0xD1, 0x20, 0xD1}) && has_seq(r_strcat.bytes, word(0x203E0)),
              r_strcat.error);
    }
    const auto r_strlen = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { str_len(0xD100); }\n", "580");
    {
        const size_t p = find_seq(r_strlen.bytes, word(0x12602));
        check("rop: str_len loads ER0 before calling strlen",
              r_strlen.ok && p != kNoPos && at(r_strlen.bytes, p + 4, 2) == seq({0x00, 0xD1}) &&
                  has_seq(r_strlen.bytes, word(0x29A2C)),
              r_strlen.error);
    }
    const auto r_setb = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { set_byte(0xD138, 0x01); }\n", "580");
    {
        const std::vector<uint8_t> pop = word(0x17B34);
        const size_t p = find_seq(r_setb.bytes, pop);
        check("rop: set_byte argument block is [addr, value, pad]",
              p != kNoPos && at(r_setb.bytes, p + 4, 4) == seq({0x38, 0xD1, 0x01, 0x00}),
              hex_of(r_setb.bytes));
        check("rop: set_byte calls [er0]=r2", has_seq(r_setb.bytes, word(0x203D2)));
    }
    const auto r_setw = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { set_word(0xD138, 0x1234); }\n", "580");
    {
        const std::vector<uint8_t> pop = word(0x17B34);
        const size_t p = find_seq(r_setw.bytes, pop);
        check("rop: set_word argument block is [addr, value]",
              p != kNoPos && at(r_setw.bytes, p + 4, 4) == seq({0x38, 0xD1, 0x34, 0x12}),
              hex_of(r_setw.bytes));
        check("rop: set_word calls [er0]=er2,rt", has_seq(r_setw.bytes, word(0x139D8)));
    }
    const auto r_addw = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { add_word(0xF039, 0x0102); }\n", "580");
    {
        const size_t p2 = find_seq(r_addw.bytes, word(0x18974));   // pop er2
        const size_t p8 = find_seq(r_addw.bytes, word(0x0A8A6));   // pop er8
        check("rop: add_word loads the delta into er2 and the address into er8",
              p2 != kNoPos && p8 == p2 + 6 &&
                  at(r_addw.bytes, p2 + 4, 2) == seq({0x02, 0x01}) &&
                  at(r_addw.bytes, p8 + 4, 2) == seq({0x39, 0xF0}),
              hex_of(r_addw.bytes));
        check("rop: add_word calls [er8]+=er2,pop xr8 and pops xr8",
              has_seq(r_addw.bytes, word(0x09CA0)) &&
                  at(r_addw.bytes, find_seq(r_addw.bytes, word(0x09CA0)) + 4, 4) ==
                      seq({0x30, 0x30, 0x30, 0x30}));
    }

    // ---------------------------------------------------------------- keytable
    const auto r_keys = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() {\n"
        "  getkey();\n"
        "  keytable(0x0480, up, 0x0840, down, 0x0000, loop);\n"
        "up: print(0x30, 0x21, \"UP\"); render(); goto loop;\n"
        "down: print(0x30, 0x31, \"DOWN\"); render(); goto loop;\n"
        "loop: delay(0x40); goto loop;\n"
        "}\n", "580");
    // getkey() must leave the scancode value in ER0, not the slot address.
    // Community sequence: pop er0 / slot / getkey / setlr / pop er0 / slot.
    const size_t pe = find_seq(r_keys.bytes, word(0x12602));   // first pop er0
    uint32_t slot_addr = 0;
    if (pe != kNoPos && pe + 6 <= r_keys.bytes.size())
        slot_addr = (uint32_t)r_keys.bytes[pe + 4] |
                    ((uint32_t)r_keys.bytes[pe + 5] << 8);
    check("rop: getkey uses an inline KI/KO slot",
          r_keys.ok && pe != kNoPos &&
              at(r_keys.bytes, pe + 6, 4) == word(0x2F5EA) &&
              at(r_keys.bytes, pe + 10, 4) == word(0x09FA8) &&
              at(r_keys.bytes, pe + 14, 4) == word(0x12602) &&
              at(r_keys.bytes, pe + 18, 2) == seq({0x00, 0x00}) &&
              slot_addr == 0xD730 + (uint32_t)pe + 18 &&
              !has_seq(r_keys.bytes, seq({0x02, 0x26, 0x31, 0x30, 0x00, 0xE8})),
          wiz::fmt("pop_er0=%zu slot=%04X expected=%04X", pe, slot_addr,
                   0xD730 + (uint32_t)pe + 18));
    {
        // getkey() clears the slot first, so a released key does not leave the
        // previous KI/KO value behind in a compiler goto loop.
        const size_t zb = pe >= 16 ? pe - 16 : kNoPos;
        check("rop: getkey clears the inline KI/KO slot",
              zb != kNoPos && at(r_keys.bytes, zb, 4) == word(0x09FA8) &&
                  at(r_keys.bytes, zb + 4, 4) == word(0x17B34) &&
                  at(r_keys.bytes, zb + 8, 2) == seq({(uint8_t)(slot_addr & 0xFF),
                                                      (uint8_t)(slot_addr >> 8)}) &&
                  at(r_keys.bytes, zb + 10, 2) == seq({0x00, 0x00}) &&
                  at(r_keys.bytes, zb + 12, 4) == word(0x139D8));
    }
    {
        const size_t ea = find_seq(r_keys.bytes, word(0x1EC0C));   // pop ea
        const size_t cmp = find_seq(r_keys.bytes, word(0x09C20));
        const size_t get = find_seq(r_keys.bytes, word(0x1C64A));
        const size_t sp = find_seq(r_keys.bytes, word(0x21F74));
        check("rop: keytable loads the table and runs cmp_ea on ER0",
              pe != kNoPos && ea == pe + 20 && cmp == ea + 6 &&
                  get == cmp + 4 && sp == get + 4,
              wiz::fmt("getkey=%zu pop_ea=%zu cmp=%zu er6=%zu sp=%zu", pe, ea, cmp, get, sp));
        check("rop: keytable table contains the key words",
              has_seq(r_keys.bytes, seq({0x80, 0x04})) &&
                  has_seq(r_keys.bytes, seq({0x40, 0x08})) &&
                  has_seq(r_keys.bytes, seq({0x00, 0x00})));
        check("rop: keytable reports the entries",
              r_keys.log.find("keytable: 3 entries") != std::string::npos, r_keys.log);
        const auto r_no_default = wiz::build_rop(
            "#rop\n#for 580\n#org E9E0\n#launcher common\n"
            "int main() {\n"
            "  getkey();\n"
            "  keytable(0x0480, up);\n"
            "up: delay(0x40); goto up;\n"
            "}\n", "580");
        check("rop: keytable warns when the 0x0000 default is missing",
              r_no_default.log.find("W005") != std::string::npos, r_no_default.log);
        // The table is part of the payload and is copied with the code, so its
        // address uses the execution address (home), right behind the final brk.
        const size_t brk = find_seq(r_keys.bytes, word(0x33030));
        uint32_t table_addr = 0;
        if (ea != kNoPos && ea + 6 <= r_keys.bytes.size())
            table_addr = (uint32_t)r_keys.bytes[ea + 4] |
                         ((uint32_t)r_keys.bytes[ea + 5] << 8);
        check("rop: keytable address points into the execution copy",
              brk != kNoPos && table_addr == 0xD730 + (uint32_t)brk + 4,
              wiz::fmt("table=%04X expected=%04X", table_addr,
                       0xD730 + (uint32_t)brk + 4));
    }
    {
        const std::string cwiz =
            "#rop\n#for 580\n#org E9E0\n#launcher common\n"
            "int main() { screen_del(); print(0x30, 0x01, \"HELLO ROP\"); render(); }\n";
        const std::string wiz_src =
            "#rop\nfor 580\n#org E9E0\n#launcher common\n"
            "def main():\n    screen_del()\n    print(0x30, 0x01, \"HELLO ROP\")\n    render()\n";
        const auto c = wiz::build_rop(cwiz, "580", "cwiz");
        const auto w = wiz::build_rop(wiz_src, "580", "wiz");
        check("rop: Wiz and CWiz syntaxes emit the same RAC chain",
              c.ok && w.ok && c.bytes == w.bytes && c.launcher == w.launcher,
              w.error.empty() ? hex_of(w.bytes) : w.error);

        const auto alias = wiz::build_rop(
            "#rop\n#for 580\nint main() { getscancode_nodelay(); }\n", "580");
        const auto short_alias = wiz::build_rop(
            "#rop\n#for 580\nint main() { get_key(); }\n", "580");
        check("rop: getkey aliases use the nonblocking inline-slot ABI",
              alias.ok && short_alias.ok && alias.bytes == short_alias.bytes,
              alias.error.empty() ? alias.log : alias.error);
        const auto render = wiz::build_rop("#rop\n#for 580\nint main() { render(); }\n", "580");
        const auto flip = wiz::build_rop("#rop\n#for 580\nint main() { flip(); }\n", "580");
        check("rop: flip aliases the display render gadget", render.ok && flip.ok &&
                  render.bytes == flip.bytes,
              flip.error);
    }
    {
        const std::string src =
            "#rop\n#for 580\nint main() { print(0x30, 0x01, \"580\"); }\n"
            "----\n#for 880\nint main() { print(0x30, 0x01, \"880\"); }\n";
        const auto r580_section = wiz::build_rop(src, "580");
        const auto r880_section = wiz::build_rop(src, "880");
        check("rop: target sections select only the matching ROP body",
              r580_section.ok && r880_section.ok &&
                  pool_offset(r580_section.bytes, "580") < r580_section.bytes.size() &&
                  pool_offset(r580_section.bytes, "880") == r580_section.bytes.size() &&
                  pool_offset(r880_section.bytes, "880") < r880_section.bytes.size() &&
                  pool_offset(r880_section.bytes, "580") == r880_section.bytes.size());

        const auto bad_getkey = wiz::build_rop(
            "#rop\n#for 580\nint main() { getkey(1); }\n", "580");
        check("rop: invalid getkey arguments are diagnosed", !bad_getkey.ok &&
                  bad_getkey.error.find("takes no arguments") != std::string::npos,
              bad_getkey.error);
        const auto bad_if = wiz::build_rop(
            "#rop\n#for 580\nint main() { if (1 < 2) { render(); } }\n", "580");
        // Literal-only conditions are folded and remain legal; a variable condition
        // is rejected rather than compiled as an unconditional branch.
        const auto dynamic_if = wiz::build_rop(
            "#rop\n#for 580\nint main() { if (flag) { render(); } }\n", "580");
        check("rop: dynamic control flow is rejected instead of miscompiled",
              bad_if.ok && !dynamic_if.ok &&
                  dynamic_if.error.find("dynamic if conditions") != std::string::npos,
              dynamic_if.error);
        const auto no_880_table = wiz::build_rop(
            "#rop\n#for 880\nint main() { getkey(); keytable(0x0480, up, 0, up); up: render(); }\n", "880");
        check("rop: unsupported 880 keytable reports a target-specific error",
              !no_880_table.ok && no_880_table.error.find("not supported") != std::string::npos,
              no_880_table.error);
    }

    // ---------------------------------------------------------------- branch target
    // Branch gadgets pop PC from "label - 2", so both goto and key tables
    // store a biased address.
    const auto r_jump = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { goto done; done: render(); }\n", "580");
    {
        const size_t pop = find_seq(r_jump.bytes, word(0x13AB0));   // pop er6
        const size_t rn = find_seq(r_jump.bytes, word(0x0947E));    // render
        uint32_t target = 0;
        if (pop != kNoPos && pop + 6 <= r_jump.bytes.size())
            target = (uint32_t)r_jump.bytes[pop + 4] |
                     ((uint32_t)r_jump.bytes[pop + 5] << 8);
        check("rop: goto target is label - 2",
              pop != kNoPos && rn != kNoPos && target == 0xD730 + (uint32_t)rn - 2,
              wiz::fmt("target=%04X expected=%04X", target, 0xD730 + (uint32_t)rn - 2));
    }

    // ---------------------------------------------------------------- 880
    const auto r880 = wiz::build_rop(hello_source(), "880");
    const size_t p880 = pool_offset(r880.bytes, "ROP ADDRESS");
    check("rop: 880 common build succeeds", r880.ok && !r880.bytes.empty());
    check("rop: 880 payload stays at execution address", r880.base == 0xE330);
    check("rop: 880 common launcher uses input area", r880.launcher_base == 0x9268);
    const auto r880_key = wiz::build_rop(
        "#rop\n#for 880\n#org E330\n#launcher common\nint main() { getkey(); }\n", "880");
    check("rop: 880 getkey uses its own gadget with an inline word slot",
          r880_key.ok && has_seq(r880_key.bytes, word(0x47C9C)) &&
              has_seq(r880_key.bytes, word(0x13B92)) &&
              r880_key.log.find("inline KI/KO slot") != std::string::npos,
          r880_key.error);
    check("rop: 880 main payload uses RAC call tags",
          r880.bytes.size() >= 4 && r880.bytes[2] == 0x30 && r880.bytes[3] == 0x30,
          r880.bytes.size() >= 4 ? wiz::fmt("%02X %02X %02X %02X", r880.bytes[0], r880.bytes[1],
                                            r880.bytes[2], r880.bytes[3]) : "short");
    check("rop: 880 pointers target in-place payload", p880 < r880.bytes.size() &&
                                                       has16(r880.bytes, 0xE330 + p880));
    // The 880 has no printline: print falls back to line_print (0x395A6,
    // segment 3).
    check("rop: 880 print falls back to line_print",
          has_seq(r880.bytes, seq({0xA6, 0x95, 0x33, 0x30})) &&
              !has_seq(r880.bytes, seq({0xC8, 0x3D, 0x32, 0x30})));
    const auto r880_small = wiz::build_rop("#rop\n#for 880\n#org E9E0\n#launcher common\n"
                                            "int main() { print_small(0x0E, 0x11, \"SMALL\"); }\n", "880");
    check("rop: 880 print_small uses the 880 smallprint",
          has_seq(r880_small.bytes, seq({0xC2, 0xFA, 0x31, 0x30})));
    const auto r880_line = wiz::build_rop("#rop\n#for 880\n#org E9E0\n#launcher common\n"
                                          "int main() { line(0, 1, 2, 3); }\n", "880");
    check("rop: 880 line uses the 880 line_draw",
          has_seq(r880_line.bytes, seq({0xFA, 0x9C, 0x30, 0x30})) &&
              has_seq(r880_line.bytes, seq({0x38, 0x64, 0x31, 0x30})));
    const auto r880_bitmap = wiz::build_rop(
        "#rop\n#for 880\n#org E9E0\n#launcher common\n"
        "int main() { bitmap(1, 2, 8, 8, \"ff8181ff\"); }\n", "880");
    check("rop: 880 bitmap uses the 880 gadgets",
          has_seq(r880_bitmap.bytes, seq({0x60, 0x7E, 0x30, 0x30})) &&
              has_seq(r880_bitmap.bytes, seq({0x58, 0x89, 0x31, 0x30})));
    // The 880 launcher stays the short jump stub: trigger, payload - 2 and
    // the "sp = er6,pop er8" gadget of the 880 (0x21344).
    // The 880 launcher stays the short jump stub.
    check("rop: 880 common launcher stays 8 bytes",
          r880.launcher.size() == 8 && r880.launcher[0] == 0xFD && r880.launcher[1] == 0x26 &&
              has16(r880.launcher, 0xE330 - 2) &&
              has_seq(r880.launcher, seq({0xC0, 0x75, 0x32, 0x30})),
          hex_of(r880.launcher));

    // ---------------------------------------------------------------- custom launcher
    const auto r_custom = wiz::build_rop("#rop\n#for 580\n#org E9E0\n"
                                         "#launcher common long D800 D900\n"
                                         "int main() { print(0x30, 0x01, \"CUSTOM\"); }\n",
                                         "580");
    const size_t p_custom = pool_offset(r_custom.bytes, "CUSTOM");
    check("rop: custom common backup is the write address",
          r_custom.ok && r_custom.base == 0xD800);
    check("rop: custom common pointers target backup address",
          r_custom.ok && p_custom < r_custom.bytes.size() &&
              has16(r_custom.bytes, 0xD800 + p_custom));
    check("rop: custom common launcher uses the given source and destination",
          r_custom.launcher == community_launcher(0x03, 0xD800, 0xD900),
          hex_of(r_custom.launcher));

    const auto rsimple = wiz::build_rop("#rop\n#launcher simple\nint main() { render(); }\n", "580");
    check("rop: simple launcher remains in-place", rsimple.ok && rsimple.base == 0xE9E0 &&
                                                        has16(rsimple.launcher, 0xE9DE));
    check("rop: simple launcher is unchanged",
          rsimple.launcher.size() == 10 && rsimple.launcher[0] == 0x78 &&
              rsimple.launcher[1] == 0x5C && rsimple.launcher[4] == 0xDE &&
              rsimple.launcher[5] == 0xE9 && rsimple.launcher[6] == 0x60 &&
              rsimple.launcher[7] == 0x0D && rsimple.launcher[8] == 0x32 &&
              rsimple.launcher[9] == 0x30,
          hex_of(rsimple.launcher));

    // ---------------------------------------------------------------- payload size
    const std::string big_text(900, 'X');
    const auto r_big = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common\n#nopad\n"
                                       "int main() { print(0x30, 0x01, \"" + big_text + "\"); }\n",
                                       "580");
    check("rop: oversized payload warns about the launcher length code",
          r_big.ok && r_big.log.find("payload too large for launcher length code") !=
                          std::string::npos,
          r_big.log);
    const auto r_big_long = wiz::build_rop("#rop\n#for 580\n#org E9E0\n#launcher common long\n#nopad\n"
                                            "int main() { print(0x30, 0x01, \"" + big_text + "\"); }\n",
                                            "580");
    check("rop: long launcher length code accepts a bigger payload",
          r_big_long.ok && r_big_long.log.find("payload too large for launcher length code") ==
                               std::string::npos,
          r_big_long.log);

    // ---------------------------------------------------------------- listing
    // The .asm listing is the low level view of the chain: address, the four
    // raw bytes, the class and the gadget.
    check("rop: listing shows the setlr gadget",
          rac_equivalent.log.find("E9E0  A8 9F 30 30   call     0x09FA8  setlr") !=
              std::string::npos,
          rac_equivalent.log);
    check("rop: listing shows the pop gadget and its data",
          rac_equivalent.log.find("E9EC  34 7B 31 30   pop      0x17B34  xr0") !=
                  std::string::npos &&
              rac_equivalent.log.find("E9F0  01 30 00 EA             data    01 30 00 EA") !=
                  std::string::npos,
          rac_equivalent.log);
    check("rop: listing shows the printline gadget",
          rac_equivalent.log.find("E9F4  C8 3D 32 30   call     0x23DC8  printline") !=
              std::string::npos,
          rac_equivalent.log);
    check("rop: listing mentions the injection and the execution address",
          rac_equivalent.log.find("injected at E9E0") != std::string::npos &&
              rac_equivalent.log.find("executed at D730") != std::string::npos,
          rac_equivalent.log);
}
