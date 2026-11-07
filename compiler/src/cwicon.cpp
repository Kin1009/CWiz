// cwicon.cpp - stock-style mode icon converter.
#include "cwicon.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace wiz {
namespace {

bool same_size(const cwmode::Image& a, const cwmode::Image& b) {
    return a.width == b.width && a.height == b.height;
}

void set_px(cwmode::Image& image, int x, int y, uint8_t v) {
    if (x < 0 || y < 0 || x >= image.width || y >= image.height) return;
    size_t i = ((size_t)y * image.width + x) * 4;
    image.rgba[i + 0] = v;
    image.rgba[i + 1] = v;
    image.rgba[i + 2] = v;
    image.rgba[i + 3] = 255;
}

void set_ink(cwmode::Image& image, int x, int y, bool ink) {
    set_px(image, x, y, ink ? 0 : 255);
}

bool source_ink(const cwmode::Image& src, int x, int y, int threshold) {
    if (x < 0 || y < 0 || x >= src.width || y >= src.height) return false;
    size_t i = ((size_t)y * src.width + x) * 4;
    const int a = src.rgba[i + 3];
    if (a < 128) return false;
    const int lum = (299 * src.rgba[i + 0] + 587 * src.rgba[i + 1] + 114 * src.rgba[i + 2]) / 1000;
    return lum < threshold;
}

std::vector<uint32_t> crc_table() {
    std::vector<uint32_t> table(256);
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[n] = c;
    }
    return table;
}

uint32_t crc32(const uint8_t* data, size_t len) {
    static const std::vector<uint32_t> table = crc_table();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

uint32_t adler32(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    for (uint8_t v : data) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

void put_chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put_be32(out, (uint32_t)data.size());
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    put_be32(out, crc32(out.data() + start, out.size() - start));
}

std::vector<uint8_t> zlib_store(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out = {0x78, 0x01};
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool final = pos + n == raw.size();
        out.push_back(final ? 0x01 : 0x00);
        out.push_back((uint8_t)(n & 0xFF));
        out.push_back((uint8_t)(n >> 8));
        out.push_back((uint8_t)(~n & 0xFF));
        out.push_back((uint8_t)((~n >> 8) & 0xFF));
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }
    put_be32(out, adler32(raw));
    return out;
}

std::string usage() {
    return "usage: cwicon --model 580|880 IN.png [--selected IN_SEL.png] -o ICON.png "
           "--raw ICON.bin [--preview-both out.png] [--threshold N] [--no-border]\n";
}

bool parse_int(const char* text, int lo, int hi, int& value) {
    char* end = nullptr;
    long v = std::strtol(text, &end, 0);
    if (!end || *end != '\0' || v < lo || v > hi) return false;
    value = (int)v;
    return true;
}

}  // namespace

IconStyle icon_style(int model) {
    IconStyle s;
    if (model == 880) {
        s.width = 64;
        s.height = 16;
    }
    return s;
}

bool is_880(int model) { return model == 880; }

cwmode::Image prepare_icon(const cwmode::Image& source, int model, int threshold,
                           bool border) {
    const IconStyle style = icon_style(model);
    cwmode::Image out;
    out.width = style.width;
    out.height = style.height;
    out.rgba.assign((size_t)out.width * out.height * 4, 255);
    for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    if (source.empty()) return out;
    if (threshold < 0) threshold = 0;
    if (threshold > 255) threshold = 255;

    const int cw = border ? style.width - 2 : style.width;
    const int ch = border ? style.height - 1 : style.height;
    const int sw = source.width;
    const int sh = source.height;
    cwmode::Image content;
    content.width = cw;
    content.height = ch;
    content.rgba.assign((size_t)cw * ch * 4, 255);
    for (size_t i = 3; i < content.rgba.size(); i += 4) content.rgba[i] = 255;
    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) set_ink(content, x, y, false);

    if (sw >= cw && sh >= ch) {
        // Larger artwork: centre-crop, no resampling.
        const int ox = (sw - cw) / 2;
        const int oy = (sh - ch) / 2;
        for (int y = 0; y < ch; y++)
            for (int x = 0; x < cw; x++)
                set_ink(content, x, y, source_ink(source, x + ox, y + oy, threshold));
    } else if (sw <= cw && sh <= ch) {
        // Smaller artwork: centre without upscaling, pad with background.
        const int ox = (cw - sw) / 2;
        const int oy = (ch - sh) / 2;
        for (int y = 0; y < sh; y++)
            for (int x = 0; x < sw; x++)
                set_ink(content, x + ox, y + oy, source_ink(source, x, y, threshold));
    } else {
        // Mixed size: scale down preserving aspect (nearest neighbour), then pad.
        const double sx = (double)cw / sw;
        const double sy = (double)ch / sh;
        const double scale = std::min(sx, sy);
        int dw = (int)(sw * scale);
        int dh = (int)(sh * scale);
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
        if (dw > cw) dw = cw;
        if (dh > ch) dh = ch;
        const int ox = (cw - dw) / 2;
        const int oy = (ch - dh) / 2;
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                const int sx0 = std::min(sw - 1, x * sw / dw);
                const int sy0 = std::min(sh - 1, y * sh / dh);
                set_ink(content, x + ox, y + oy, source_ink(source, sx0, sy0, threshold));
            }
    }

    for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
            const size_t i = ((size_t)y * cw + x) * 4;
            const bool ink = content.rgba[i] == 0;
            set_ink(out, x, y, ink);
        }
    if (border) {
        // Stock cell separator: one pixel on the right and bottom edges.
        for (int y = 1; y < style.height - 1; y++) set_ink(out, style.width - 2, y, true);
        for (int x = 1; x < style.width - 2; x++) set_ink(out, x, style.height - 1, true);
        // Shortcut-key box.  Measured from the stock 48x23 bitmaps (menu table
        // at 0x7FF8): x 38..46, y 12..21, with the top-left pixel rounded off.
        // The firmware draws the key digit in this white box in black, so the
        // box itself is empty.  It is painted after the artwork.
        const int bx0 = is_880(model) ? 54 : 38;
        const int bx1 = is_880(model) ? 62 : 46;
        const int by0 = is_880(model) ? 8 : 12;
        const int by1 = is_880(model) ? 14 : 21;
        for (int y = by0; y <= by1; y++)
            for (int x = bx0; x <= bx1; x++) set_ink(out, x, y, true);
        set_ink(out, bx0, by0, false);
    }
    return out;
}

std::vector<uint8_t> pack_icon(const cwmode::Image& prepared, int model) {
    const IconStyle style = icon_style(model);
    if (prepared.width != style.width || prepared.height != style.height) return {};
    if (is_880(model)) {
        std::vector<uint8_t> raw(0x100, 0);
        const int w2 = style.width / 8;
        for (int y = 0; y < style.height; y++)
            for (int x = 0; x < style.width; x++) {
                const size_t i = ((size_t)y * prepared.width + x) * 4;
                const bool ink = prepared.rgba[i] == 0;
                const uint8_t bit = (uint8_t)(0x80 >> (x & 7));
                if (ink) {
                    raw[y * w2 + (x >> 3)] |= bit;
                    raw[0x80 + y * w2 + (x >> 3)] |= bit;
                }
            }
        return raw;
    }
    std::vector<uint8_t> raw(0x8A, 0);
    const int w2 = style.width / 8;
    for (int y = 0; y < style.height; y++)
        for (int x = 0; x < style.width; x++) {
            const size_t i = ((size_t)y * prepared.width + x) * 4;
            if (prepared.rgba[i] == 0) raw[y * w2 + (x >> 3)] |= (uint8_t)(0x80 >> (x & 7));
        }
    return raw;
}

cwmode::Image unpack_icon(const std::vector<uint8_t>& raw, int model) {
    const IconStyle style = icon_style(model);
    cwmode::Image out;
    out.width = style.width;
    out.height = style.height;
    out.rgba.assign((size_t)style.width * style.height * 4, 255);
    const int w2 = style.width / 8;
    const bool two = is_880(model);
    if (raw.size() != (two ? 0x100u : 0x8Au)) return out;
    for (int y = 0; y < style.height; y++)
        for (int x = 0; x < style.width; x++) {
            const size_t byte = (size_t)y * w2 + (x >> 3);
            const uint8_t bit = (uint8_t)(0x80 >> (x & 7));
            bool ink = (raw[byte] & bit) != 0;
            if (two) ink = (raw[0x80 + byte] & bit) != 0;
            set_ink(out, x, y, ink);
        }
    return out;
}

std::vector<uint8_t> invert_packed_icon(const std::vector<uint8_t>& raw) {
    std::vector<uint8_t> out = raw;
    for (uint8_t& b : out) b = (uint8_t)~b;
    return out;
}

cwmode::Image compose_preview(const cwmode::Image& unselected, const cwmode::Image& selected,
                              int scale, int gap) {
    cwmode::Image out;
    if (!same_size(unselected, selected) || scale < 1) return out;
    out.width = unselected.width * scale * 2 + gap;
    out.height = unselected.height * scale;
    out.rgba.assign((size_t)out.width * out.height * 4, 255);
    auto blit = [&](const cwmode::Image& src, int ox) {
        for (int y = 0; y < out.height; y++)
            for (int x = 0; x < src.width * scale; x++) {
                const size_t si = ((size_t)(y / scale) * src.width + x / scale) * 4;
                set_px(out, ox + x, y, src.rgba[si]);
            }
    };
    blit(unselected, 0);
    blit(selected, unselected.width * scale + gap);
    return out;
}

bool write_png(const std::string& path, const cwmode::Image& image, std::string& error) {
    if (image.empty()) {
        error = "cannot write an empty PNG";
        return false;
    }
    std::vector<uint8_t> raw;
    raw.reserve((size_t)image.height * (1 + image.width * 4));
    for (int y = 0; y < image.height; y++) {
        raw.push_back(0);
        const size_t start = (size_t)y * image.width * 4;
        raw.insert(raw.end(), image.rgba.begin() + start,
                   image.rgba.begin() + start + (size_t)image.width * 4);
    }
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    std::vector<uint8_t> ihdr;
    put_be32(ihdr, (uint32_t)image.width);
    put_be32(ihdr, (uint32_t)image.height);
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    put_chunk(png, "IHDR", ihdr);
    put_chunk(png, "IDAT", zlib_store(raw));
    put_chunk(png, "IEND", {});
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot write image: " + path;
        return false;
    }
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    if (!ok) error = "short write to " + path;
    return ok;
}

bool write_raw(const std::string& path, const std::vector<uint8_t>& data, std::string& error) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        error = "cannot write raw file: " + path;
        return false;
    }
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!ok) error = "short write to " + path;
    return ok;
}

int cwicon_main(int argc, char** argv) {
    std::string input, selected_path, out_png, out_raw, preview_path;
    int model = 0;
    int threshold = 128;
    bool border = true;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        auto value = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "cwicon: %s needs a value\n%s", what, usage().c_str());
                return nullptr;
            }
            return argv[++i];
        };
        if (a == "--model") {
            const char* v = value("--model");
            if (!v) return 1;
            if (std::strcmp(v, "580") == 0 || std::strcmp(v, "cw") == 0) model = 580;
            else if (std::strcmp(v, "880") == 0 || std::strcmp(v, "cwii") == 0) model = 880;
            else {
                std::fprintf(stderr, "cwicon: --model must be 580 or 880\n");
                return 1;
            }
        } else if (a == "--selected") {
            const char* v = value("--selected");
            if (!v) return 1;
            selected_path = v;
        } else if (a == "-o") {
            const char* v = value("-o");
            if (!v) return 1;
            out_png = v;
        } else if (a == "--raw") {
            const char* v = value("--raw");
            if (!v) return 1;
            out_raw = v;
        } else if (a == "--preview-both") {
            const char* v = value("--preview-both");
            if (!v) return 1;
            preview_path = v;
        } else if (a == "--threshold") {
            const char* v = value("--threshold");
            if (!v) return 1;
            if (!parse_int(v, 0, 255, threshold)) {
                std::fprintf(stderr, "cwicon: --threshold must be 0..255\n");
                return 1;
            }
        } else if (a == "--no-border") {
            border = false;
        } else if (a == "-h" || a == "--help") {
            std::fputs(usage().c_str(), stdout);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "cwicon: unknown option '%s'\n%s", a.c_str(), usage().c_str());
            return 1;
        } else if (input.empty()) {
            input = a;
        } else {
            std::fprintf(stderr, "cwicon: unexpected argument '%s'\n%s", a.c_str(),
                         usage().c_str());
            return 1;
        }
    }
    if (model == 0 || input.empty() || out_png.empty() || out_raw.empty()) {
        std::fputs(usage().c_str(), stderr);
        return 1;
    }

    cwmode::Image source;
    std::string error;
    if (!cwmode::load_image(input, source, error)) {
        std::fprintf(stderr, "cwicon: %s\n", error.c_str());
        return 1;
    }
    const cwmode::Image prepared = prepare_icon(source, model, threshold, border);
    const std::vector<uint8_t> raw = pack_icon(prepared, model);
    const std::vector<uint8_t> raw_selected = invert_packed_icon(raw);
    cwmode::Image selected = unpack_icon(raw_selected, model);
    if (!selected_path.empty()) {
        cwmode::Image sel_src;
        if (!cwmode::load_image(selected_path, sel_src, error)) {
            std::fprintf(stderr, "cwicon: %s\n", error.c_str());
            return 1;
        }
        selected = prepare_icon(sel_src, model, threshold, border);
        if (!border) {
            // The selected preview still follows the input; no overlay.
        }
    }
    if (!write_png(out_png, prepared, error) || !write_raw(out_raw, raw, error)) {
        std::fprintf(stderr, "cwicon: %s\n", error.c_str());
        return 1;
    }
    if (!preview_path.empty()) {
        const cwmode::Image preview = compose_preview(prepared, selected);
        if (preview.empty() || !write_png(preview_path, preview, error)) {
            std::fprintf(stderr, "cwicon: %s\n", error.empty() ? "cannot build preview" : error.c_str());
            return 1;
        }
    }
    return 0;
}

}  // namespace wiz
