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

    const auto r580 = wiz::build_rop(hello_source(), "580");
    const size_t p580 = pool_offset(r580.bytes, "ROP ADDRESS");
    check("rop: 580 common build succeeds", r580.ok && !r580.bytes.empty());
    check("rop: 580 payload stays at injection address", r580.base == 0xE9E0);
    check("rop: 580 common launcher uses input area", r580.launcher_base == 0xD180);
    check("rop: 580 pointers target copied payload", p580 < r580.bytes.size() &&
                                                       has16(r580.bytes, 0xD730 + p580));
    check("rop: 580 pointers do not target stale injection address",
          p580 >= r580.bytes.size() || !has16(r580.bytes, 0xE9E0 + p580));

    const auto r880 = wiz::build_rop(hello_source(), "880");
    const size_t p880 = pool_offset(r880.bytes, "ROP ADDRESS");
    check("rop: 880 common build succeeds", r880.ok && !r880.bytes.empty());
    check("rop: 880 payload stays at execution address", r880.base == 0xE330);
    check("rop: 880 common launcher uses input area", r880.launcher_base == 0x9268);
    check("rop: 880 pointers target in-place payload", p880 < r880.bytes.size() &&
                                                       has16(r880.bytes, 0xE330 + p880));

    const auto r_custom = wiz::build_rop("#rop\n#for 580\n#org E9E0\n"
                                         "#launcher common long D800 D900\n"
                                         "int main() { print(0x30, 0x01, \"CUSTOM\"); }\n",
                                         "580");
    const size_t p_custom = pool_offset(r_custom.bytes, "CUSTOM");
    check("rop: custom common backup is the write address",
          r_custom.ok && r_custom.base == 0xD800);
    check("rop: custom common main is the relocation address",
          r_custom.ok && p_custom < r_custom.bytes.size() &&
              has16(r_custom.bytes, 0xD900 + p_custom));

    const auto rsimple = wiz::build_rop("#rop\n#launcher simple\nint main() { render(); }\n", "580");
    check("rop: simple launcher remains in-place", rsimple.ok && rsimple.base == 0xE9E0 &&
                                                        has16(rsimple.launcher, 0xE9DE));
}
