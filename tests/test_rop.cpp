#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "harness.hpp"
#include "rop.hpp"

using namespace wt;

namespace {

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

const char* hello_source() {
    return "#rop\n"
           "#launcher common\n"
           "int main() {\n"
           "    print(0x30, 0x01, \"ROP ADDRESS\");\n"
           "    render();\n"
           "}\n";
}

}  // namespace

void wt::run_rop() {
    check("rop: source marker selects backend", wiz::source_is_rop(hello_source()));

    // Cross-compiled with RAC/hdcompiler's equivalent low-level source:
    // The compiler supplies RAC's setlr; setsfr preamble, then emits
    // buffer_clear; line_print("HELLO ROP"); render.ddd4.
    const auto rac_equivalent = wiz::build_rop(
        "#rop\n#for 580\n#org E9E0\n#launcher common\n"
        "int main() { screen_del(); print(0x30, 0x01, \"HELLO ROP\"); "
        "render(); }\n", "580");
    const std::vector<uint8_t> rac_bytes = {
        0xA8, 0x9F, 0x30, 0x30, 0xE0, 0xA0, 0x30, 0x30,
        0x60, 0x8C, 0x30, 0x30,
        0x34, 0x7B, 0x31, 0x30, 0x30, 0x01, 0xFC, 0xE9,
        0x80, 0x8F, 0x30, 0x30, 0x7E, 0x94, 0x30, 0x30,
        0x48, 0x45, 0x4C, 0x4C,
        0x4F, 0x20, 0x52, 0x4F, 0x50, 0x00};
    check("rop: Wiz/RAC equivalent emits identical 580 bytes",
          rac_equivalent.ok && rac_equivalent.bytes == rac_bytes);
    const auto legacy_setup = wiz::build_rop(
        "#rop\n#org E9E0\nint main() { setlr_pc(); setsfr(); screen_del(); }\n", "580");
    check("rop: legacy setlr_pc spelling uses RAC setlr", legacy_setup.ok &&
          legacy_setup.bytes.size() >= 4 && legacy_setup.bytes[0] == 0xA8 &&
          legacy_setup.bytes[1] == 0x9F);
    const std::vector<uint8_t> rac_launcher = {
        0xFD, 0x24, 0x30, 0x30, 0xD0, 0x08, 0x33, 0x30,
        0x34, 0x7B, 0x31, 0x30, 0x30, 0xD7, 0xE0, 0xE9,
        0x51, 0x94, 0x30, 0x30, 0xFE, 0x02, 0x42, 0x07,
        0x31, 0x30, 0x2E, 0xD7, 0x60, 0x0D, 0x32, 0x30};
    check("rop: 580 common launcher matches RAC", rac_equivalent.launcher == rac_launcher);

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

    const auto r880 = wiz::build_rop(hello_source(), "880");
    const size_t p880 = pool_offset(r880.bytes, "ROP ADDRESS");
    check("rop: 880 common build succeeds", r880.ok && !r880.bytes.empty());
    check("rop: 880 payload stays at execution address", r880.base == 0xE330);
    check("rop: 880 common launcher uses input area", r880.launcher_base == 0x9268);
    check("rop: 880 main payload uses RAC call tags",
          r880.bytes.size() >= 4 && r880.bytes[2] == 0x30 && r880.bytes[3] == 0x30,
          r880.bytes.size() >= 4 ? wiz::fmt("%02X %02X %02X %02X", r880.bytes[0], r880.bytes[1],
                                            r880.bytes[2], r880.bytes[3]) : "short");
    check("rop: 880 pointers target in-place payload", p880 < r880.bytes.size() &&
                                                       has16(r880.bytes, 0xE330 + p880));

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

    const auto rsimple = wiz::build_rop("#rop\n#launcher simple\nint main() { render(); }\n", "580");
    check("rop: simple launcher remains in-place", rsimple.ok && rsimple.base == 0xE9E0 &&
                                                        has16(rsimple.launcher, 0xE9DE));
}
