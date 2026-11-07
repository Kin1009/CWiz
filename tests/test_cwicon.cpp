// test_cwicon.cpp - stock-style icon converter and the three-argument #cwapp form.
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "builtin_rom.hpp"
#include "cwicon.hpp"
#include "cwmode.hpp"
#include "harness.hpp"
#include "sha256.hpp"

using namespace wt;
using namespace wiz;

namespace {

cwmode::Image load(const std::string& path) {
    cwmode::Image image;
    std::string error;
    if (!cwmode::load_image(root_path(path), image, error))
        check("test setup: load " + path, false, error);
    return image;
}

bool is_ink(const cwmode::Image& image, int x, int y) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return false;
    const size_t i = ((size_t)y * image.width + x) * 4;
    return image.rgba[i + 3] == 255 && image.rgba[i] == 0;
}

std::string sibling(const char* name) {
    return (std::filesystem::path(g_wizc()).parent_path() / name).string();
}

int run_cli(const std::vector<std::string>& args, std::string* out) {
    std::string cmd = "'" + sibling("cwicon") + "'";
    for (const auto& a : args) cmd += " '" + a + "'";
    cmd += " 2>&1";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return -1;
    char buf[4096];
    while (std::fgets(buf, sizeof buf, p)) {
        if (out) *out += buf;
    }
    int st = pclose(p);
    return WIFEXITED(st) ? WEXITSTATUS(st) : st;
}

}  // namespace

void wt::run_cwicon() {
    const cwmode::Image logo = load("tests/data/cwicon/logo_48x23.png");
    const cwmode::Image big = load("tests/data/cwicon/big_96x46.png");
    const cwmode::Image small = load("tests/data/cwicon/small_24x12.png");
    const cwmode::Image wide = load("tests/data/cwicon/wide_96x10.png");
    const cwmode::Image gray = load("tests/data/cwicon/gray_48x23.png");
    const cwmode::Image icon880 = load("tests/data/cwicon/icon_880_64x16.png");
    check("cwicon: fixtures decode", !logo.empty() && !big.empty() && !small.empty() &&
                                         !wide.empty() && !gray.empty() && !icon880.empty());

    const cwmode::Image prepared = prepare_icon(logo, 580, 128, true);
    check("cwicon 580: output is 48x23", prepared.width == 48 && prepared.height == 23);
    check("cwicon 580: stock right/bottom cell separator",
          is_ink(prepared, 46, 1) && is_ink(prepared, 46, 21) && is_ink(prepared, 1, 22) &&
              is_ink(prepared, 45, 22) && !is_ink(prepared, 46, 0) && !is_ink(prepared, 0, 22));
    const std::vector<uint8_t> raw = pack_icon(prepared, 580);
    check("cwicon 580: packed length is 0x8A", raw.size() == 0x8A);
    // Stock key box: x 38..46, y 12..21, with the top-left pixel rounded off.
    // The golden mask locks the exact border/box geometry.
    std::vector<uint8_t> mask(0x8A, 0);
    for (int y = 1; y <= 21; y++) mask[y * 6 + (46 >> 3)] |= (uint8_t)(0x80 >> (46 & 7));
    for (int x = 1; x <= 45; x++) mask[22 * 6 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
    for (int y = 12; y <= 21; y++)
        for (int x = 38; x <= 46; x++) mask[y * 6 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
    mask[12 * 6 + (38 >> 3)] &= (uint8_t) ~(0x80 >> (38 & 7));
    check("cwicon 580: border/key-box mask golden hash",
          wt::sha256_hex(mask) ==
              "1788b7b79495b13f1bfd930234e7196aecaa1379c8c03e7dcadcd56a28fb119c",
          wt::sha256_hex(mask));
    bool box_ok = true;
    for (size_t i = 0; i < raw.size(); i++)
        if ((raw[i] & mask[i]) != mask[i]) box_ok = false;
    check("cwicon 580: border and key box are painted over the artwork", box_ok);
    const std::vector<uint8_t> stock_raw = [&] {
        const std::vector<uint8_t> rom(wiz::kFx580VnRom,
                                      wiz::kFx580VnRom + wiz::kFx580VnRomSize);
        const int off = rom[0x7FF8 + 6] | (rom[0x7FF8 + 7] << 8);  // slot 2
        return std::vector<uint8_t>(rom.begin() + off, rom.begin() + off + 0x8A);
    }();
    bool stock_box = true;
    for (size_t i = 0; i < mask.size(); i++)
        if ((stock_raw[i] & mask[i]) != mask[i]) stock_box = false;
    check("cwicon 580: measured box matches the stock slot-2 bitmap", stock_box);

    const cwmode::Image round = unpack_icon(raw, 580);
    bool same = round.width == prepared.width && round.height == prepared.height;
    for (size_t i = 0; same && i < prepared.rgba.size(); i += 4)
        if (prepared.rgba[i] != round.rgba[i]) same = false;
    check("cwicon 580: pack/unpack is pixel-exact", same);

    const cwmode::Image prepared_twice = prepare_icon(prepared, 580, 128, true);
    bool idempotent = prepared_twice.width == prepared.width &&
                      prepared_twice.height == prepared.height;
    for (size_t i = 0; idempotent && i < prepared.rgba.size(); i += 4)
        if (prepared.rgba[i] != prepared_twice.rgba[i]) idempotent = false;
    check("cwicon 580: decorating an already decorated icon is idempotent", idempotent);

    const cwmode::Image inverse = cwmode::invert_image(prepared);
    const std::vector<uint8_t> raw_inverse = pack_icon(inverse, 580);
    bool complement = raw.size() == raw_inverse.size();
    for (size_t i = 0; complement && i < raw.size(); i++)
        if (raw_inverse[i] != (uint8_t)~raw[i]) complement = false;
    check("cwicon 580: inverse is the bitwise complement of the packed icon", complement);

    // Threshold edges: 127 is ink at 128, 128 is background; 0 inks nothing and
    // 255 leaves pure white as background.
    cwmode::Image t = prepare_icon(gray, 580, 128, false);
    check("cwicon threshold 128: 127 is ink, 128 is background",
          is_ink(t, 4 * 4, 1) && !is_ink(t, 5 * 4, 1));
    t = prepare_icon(gray, 580, 0, false);
    bool none = true;
    for (int x = 0; x < 48; x++)
        if (is_ink(t, x, 1)) none = false;
    check("cwicon threshold 0: nothing is ink", none);
    t = prepare_icon(gray, 580, 255, false);
    check("cwicon threshold 255: 254 is ink, 255 is background",
          is_ink(t, 8 * 4, 1) && !is_ink(t, 9 * 4, 1));

    // Crop: oversized artwork is centre-cropped.  Pad: small artwork is centred.
    const cwmode::Image big_prepared = prepare_icon(big, 580, 128, true);
    check("cwicon crop: a 96x46 source fills the 46x22 content box",
          is_ink(big_prepared, 0, 1) && is_ink(big_prepared, 45, 1));
    const cwmode::Image small_prepared = prepare_icon(small, 580, 128, true);
    check("cwicon pad: a 24x12 source is centred with background margins",
          !is_ink(small_prepared, 1, 1) && is_ink(small_prepared, 17, 7) &&
              !is_ink(small_prepared, 44, 5) && is_ink(small_prepared, 44, 20));
    const cwmode::Image wide_prepared = prepare_icon(wide, 580, 128, true);
    check("cwicon aspect: a 96x10 source is scaled to fit and padded",
          !is_ink(wide_prepared, 1, 1) && is_ink(wide_prepared, 20, 11));

    const std::string raw_path = root_path("temp/cwicon/test.raw");
    const std::string png_path = root_path("temp/cwicon/test.png");
    const std::string both_path = root_path("temp/cwicon/test-both.png");
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(raw_path).parent_path(), ec);
    std::string error;
    check("cwicon: PNG writer round-trips",
          write_png(png_path, prepared, error) && cwmode::load_image(png_path, t, error) &&
              t.width == 48 && t.height == 23 && is_ink(t, 46, 1));
    check("cwicon: raw writer", write_raw(raw_path, raw, error));
    const cwmode::Image both = compose_preview(prepared, inverse);
    check("cwicon: preview-both is two scaled icons plus a gap",
          both.width == 48 * 4 * 2 + 4 && both.height == 23 * 4);

    // 880: 64x16, two planes, 0x100 bytes, identical planes for binary art.
    const cwmode::Image p880 = prepare_icon(icon880, 880, 128, true);
    check("cwicon 880: output is 64x16", p880.width == 64 && p880.height == 16);
    const std::vector<uint8_t> raw880 = pack_icon(p880, 880);
    check("cwicon 880: packed length is 0x100", raw880.size() == 0x100);
    bool planes = true;
    for (size_t i = 0; i < 0x80; i++)
        if (raw880[i] != raw880[0x80 + i]) planes = false;
    check("cwicon 880: binary art uses the same bits in both planes", planes);
    const cwmode::Image round880 = unpack_icon(raw880, 880);
    bool same880 = round880.width == 64 && round880.height == 16;
    for (size_t i = 0; same880 && i < p880.rgba.size(); i += 4)
        if (p880.rgba[i] != round880.rgba[i]) same880 = false;
    check("cwicon 880: pack/unpack is pixel-exact", same880);
    const std::vector<uint8_t> inv880 = invert_packed_icon(raw880);
    bool complement880 = inv880.size() == raw880.size();
    for (size_t i = 0; complement880 && i < raw880.size(); i++)
        if (inv880[i] != (uint8_t)~raw880[i]) complement880 = false;
    check("cwicon 880: selected variant complements both planes", complement880);

    // The C++ compiler and the CLI pack the same prepared image identically.
    {
        const std::vector<uint8_t> base(wiz::kFx580VnRom,
                                        wiz::kFx580VnRom + wiz::kFx580VnRomSize);
        cwmode::ModeSpec spec;
        spec.code = 0xA0;
        spec.icon = prepared;
        spec.names = {"ICON", "ICON"};
        spec.entry = 0x30E40;
        cwmode::Options options;
        options.model = cwmode::Model::Cw580;
        cwmode::PatchResult cr = cwmode::patch(base, {spec}, options);
        bool same_raw = cr.ok && !cr.icon_addr.empty();
        if (same_raw) {
            const uint32_t at = cr.icon_addr[0];
            for (size_t i = 0; same_raw && i < raw.size(); i++)
                if (cr.rom[at + i] != raw[i]) same_raw = false;
        }
        check("cwicon: packed bytes match the compiler's mode icon", same_raw,
              cr.ok ? "" : cr.error);
    }

    // Stock icon round-trip from the bundled ROM menu table at 0x7FF8.
    {
        const std::vector<uint8_t> rom(wiz::kFx580VnRom, wiz::kFx580VnRom + wiz::kFx580VnRomSize);
        const int icon_off = rom[0x7FF8 + 6 * 7] | (rom[0x7FF8 + 6 * 7 + 1] << 8);
        std::vector<uint8_t> stock(rom.begin() + icon_off, rom.begin() + icon_off + 0x8A);
        const cwmode::Image stock_img = unpack_icon(stock, 580);
        check("cwicon: a stock 580 icon unpacks to 48x23",
              stock_img.width == 48 && stock_img.height == 23);
        check("cwicon: stock icon pack/unpack round-trips", pack_icon(stock_img, 580) == stock,
              wiz::fmt("offset %05X", icon_off));
    }

    // CLI: the documented command shape.
    const std::string cli_png = root_path("temp/cwicon/cli.png");
    const std::string cli_raw = root_path("temp/cwicon/cli.bin");
    const std::string cli_both = root_path("temp/cwicon/cli-both.png");
    std::string log;
    int rc = run_cli({"--model", "580", root_path("tests/data/cwicon/logo_48x23.png"), "-o",
                      cli_png, "--raw", cli_raw, "--preview-both", cli_both},
                     &log);
    check("cwicon CLI: builds PNG, raw and preview", rc == 0, log);
    bool ok = false;
    const std::vector<uint8_t> cli_bytes = wiz::read_binary_file(cli_raw, &ok);
    check("cwicon CLI: raw file is 0x8A bytes", ok && cli_bytes.size() == 0x8A);
    check("cwicon CLI: PNG is 48x23", cwmode::load_image(cli_png, t, error) && t.width == 48);
    log.clear();
    rc = run_cli({"--model", "888", root_path("tests/data/cwicon/logo_48x23.png"), "-o",
                  cli_png, "--raw", cli_raw},
                 &log);
    check("cwicon CLI: unknown model is rejected", rc != 0 && log.find("580 or 880") != std::string::npos,
          log);
}
