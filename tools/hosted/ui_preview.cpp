#include "../../components/ui/canvas.hpp"

#include <stdint.h>
#include <stdio.h>

using jani::ui::Rect;
using jani::ui::Surface;

namespace {

constexpr uint32_t width = 1024;
constexpr uint32_t height = 640;
uint32_t pixels[width * height];

constexpr uint32_t ink = 0x203236;
constexpr uint32_t muted = 0x627478;
constexpr uint32_t paper = 0xf5f7f8;
constexpr uint32_t white = 0xffffff;
constexpr uint32_t border = 0xd7e0e1;
constexpr uint32_t teal = 0x187a70;
constexpr uint32_t pale_teal = 0xe1efeb;
constexpr uint32_t coral = 0xe16b55;

bool render(const Surface &surface) {
    using jani::ui::draw_text;
    using jani::ui::fill_rect;

    if (!fill_rect(surface, Rect{0, 0, width, height}, paper)) return false;
    if (!fill_rect(surface, Rect{0, 0, width, 60}, ink)) return false;
    if (!fill_rect(surface, Rect{0, 60, 206, 548}, 0xecf1f0)) return false;
    if (!fill_rect(surface, Rect{205, 60, 1, 548}, border)) return false;
    if (!fill_rect(surface, Rect{0, 608, width, 32}, ink)) return false;

    if (!draw_text(surface, 25, 20, "JANI", 4, white, 2)) return false;
    if (!draw_text(surface, 866, 27, "WORKSPACE", 9, 0xc9d9d8, 1)) return false;
    if (!fill_rect(surface, Rect{983, 27, 8, 8}, coral)) return false;

    if (!draw_text(surface, 27, 88, "LIBRARY", 7, muted, 1)) return false;
    if (!fill_rect(surface, Rect{0, 117, 206, 43}, pale_teal)) return false;
    if (!fill_rect(surface, Rect{0, 117, 4, 43}, teal)) return false;
    if (!draw_text(surface, 27, 132, "OBJECTS", 7, ink, 2)) return false;
    if (!draw_text(surface, 27, 186, "ACTIVITY", 8, muted, 1)) return false;
    if (!draw_text(surface, 27, 224, "SETTINGS", 8, muted, 1)) return false;
    if (!fill_rect(surface, Rect{27, 563, 150, 1}, border)) return false;
    if (!draw_text(surface, 27, 577, "LOCAL STORE", 11, muted, 1)) return false;

    if (!draw_text(surface, 250, 100, "OBJECTS", 7, ink, 3)) return false;
    if (!draw_text(surface, 251, 139, "LOCAL STORE", 11, muted, 1)) return false;

    if (!fill_rect(surface, Rect{250, 180, 504, 43}, white)) return false;
    if (!fill_rect(surface, Rect{250, 222, 504, 1}, border)) return false;
    if (!draw_text(surface, 269, 195, "SEARCH OBJECTS", 14, muted, 1)) return false;
    if (!fill_rect(surface, Rect{770, 180, 128, 43}, teal)) return false;
    if (!fill_rect(surface, Rect{790, 201, 14, 2}, white)) return false;
    if (!fill_rect(surface, Rect{796, 195, 2, 14}, white)) return false;
    if (!draw_text(surface, 818, 195, "NEW", 3, white, 2)) return false;

    if (!draw_text(surface, 251, 249, "NAME", 4, muted, 1)) return false;
    if (!draw_text(surface, 817, 249, "STATE", 5, muted, 1)) return false;
    if (!fill_rect(surface, Rect{250, 269, 710, 1}, border)) return false;

    constexpr const char *names[] = {"NOTES", "PROJECTS", "ARCHIVE", "SYSTEM"};
    constexpr size_t lengths[] = {5, 8, 7, 6};
    constexpr const char *details[] = {"EDITED TODAY", "LOCAL OBJECTS", "SNAPSHOTS", "CONFIGURATION"};
    constexpr size_t detail_lengths[] = {12, 13, 9, 13};
    for (uint32_t i = 0; i < 4; ++i) {
        const int32_t y = 270 + static_cast<int32_t>(i) * 75;
        if (!fill_rect(surface, Rect{250, y, 710, 74}, white)) return false;
        if (!fill_rect(surface, Rect{250, y + 74, 710, 1}, border)) return false;
        if (!fill_rect(surface, Rect{271, y + 23, 22, 22}, i == 1 ? coral : teal)) return false;
        if (!draw_text(surface, 312, y + 20, names[i], lengths[i], ink, 2)) return false;
        if (!draw_text(surface, 312, y + 47, details[i], detail_lengths[i], muted, 1)) return false;
        if (!draw_text(surface, 816, y + 30, "LOCAL", 5, teal, 1)) return false;
    }

    if (!draw_text(surface, 25, 619, "JANI OS", 7, 0xc9d9d8, 1)) return false;
    if (!draw_text(surface, 908, 619, "OFFLINE", 7, 0xf2c2b8, 1)) return false;
    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    const Surface surface{pixels, width * height, width, height, width};
    if (!render(surface)) return 1;

    FILE *file = fopen(argv[1], "wb");
    if (file == nullptr) return 1;
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(file);
        return 1;
    }

    uint8_t row[width * 3];
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t rgb = pixels[y * width + x];
            row[x * 3] = static_cast<uint8_t>(rgb >> 16);
            row[x * 3 + 1] = static_cast<uint8_t>(rgb >> 8);
            row[x * 3 + 2] = static_cast<uint8_t>(rgb);
        }
        if (fwrite(row, 1, sizeof(row), file) != sizeof(row)) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}
