// cwicon.hpp - convert ordinary artwork into stock-style fx-580VN X / fx-880BTG
// mode icons and the packed bytes the firmware stores.
//
// The 580 mode icon is a 48x23 1-bit bitmap (0x8A bytes).  The 880 icon is a
// 64x16 two-plane bitmap (0x100 bytes: plane 0 then plane 1).  Stock menu cells
// carry a one-pixel separator on the right and bottom edges; the converter
// reproduces it by default (--no-border disables it).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cwmode.hpp"

namespace wiz {

struct IconStyle {
    int width = 48;
    int height = 23;
    int threshold = 128;  // ink when luminance < threshold
    bool border = true;   // add the stock right/bottom cell separator
};

IconStyle icon_style(int model);  // 580 -> 48x23, 880 -> 64x16
bool is_880(int model);

// Threshold, crop/pad and (optionally) draw the stock border.  The result is an
// opaque black/white RGBA image at the exact icon size.
cwmode::Image prepare_icon(const cwmode::Image& source, int model, int threshold,
                           bool border);

// Packed firmware bytes: 0x8A for 580, 0x100 for 880.  Bits set mean ink.
std::vector<uint8_t> pack_icon(const cwmode::Image& prepared, int model);
cwmode::Image unpack_icon(const std::vector<uint8_t>& raw, int model);

// Bitwise complement of packed bytes (the selected variant).
std::vector<uint8_t> invert_packed_icon(const std::vector<uint8_t>& raw);

// Side-by-side unselected/selected preview, integer-scaled with a white gap.
cwmode::Image compose_preview(const cwmode::Image& unselected, const cwmode::Image& selected,
                              int scale = 4, int gap = 4);

bool write_png(const std::string& path, const cwmode::Image& image, std::string& error);
bool write_raw(const std::string& path, const std::vector<uint8_t>& data, std::string& error);

// Command line entry point used by main_cwicon.cpp.
int cwicon_main(int argc, char** argv);

}  // namespace wiz
