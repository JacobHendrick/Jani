#include "canvas.hpp"

namespace jani::ui {

namespace {

constexpr uint8_t upper[26][7] = {
    {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110},
    {0b01111, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b01111},
    {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110},
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111},
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000},
    {0b01111, 0b10000, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111},
    {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b11111},
    {0b00111, 0b00010, 0b00010, 0b00010, 0b10010, 0b10010, 0b01100},
    {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001},
    {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111},
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001},
    {0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001, 0b10001},
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000},
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101},
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001},
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110},
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},
    {0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b01010, 0b00100},
    {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010},
    {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001},
    {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100},
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111},
};

constexpr uint8_t digits[10][7] = {
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
    {0b11110, 0b00001, 0b00001, 0b01110, 0b00001, 0b00001, 0b11110},
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},
    {0b11111, 0b10000, 0b10000, 0b11110, 0b00001, 0b00001, 0b11110},
    {0b01111, 0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00001, 0b11110},
};

constexpr uint8_t blank[7] = {};
constexpr uint8_t dash[7] = {0, 0, 0, 0b11111, 0, 0, 0};
constexpr uint8_t dot[7] = {0, 0, 0, 0, 0, 0b00100, 0b00100};
constexpr uint8_t colon[7] = {0, 0b00100, 0b00100, 0, 0b00100, 0b00100, 0};
constexpr uint8_t question[7] = {
    0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0, 0b00100,
};

const uint8_t *glyph(char ch) noexcept {
    if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
    if (ch >= 'A' && ch <= 'Z') return upper[ch - 'A'];
    if (ch >= '0' && ch <= '9') return digits[ch - '0'];
    switch (ch) {
    case ' ': return blank;
    case '-': return dash;
    case '.': return dot;
    case ':': return colon;
    default: return question;
    }
}

} // namespace

bool valid_surface(const Surface &surface) noexcept {
    return surface.pixels != nullptr &&
           surface.width != 0 && surface.height != 0 &&
           surface.stride >= surface.width &&
           surface.height <= surface.pixel_count / surface.stride;
}

bool contains_point(Rect rect, int32_t x, int32_t y) noexcept {
    const int64_t right = static_cast<int64_t>(rect.x) + rect.width;
    const int64_t bottom = static_cast<int64_t>(rect.y) + rect.height;

    return x >= rect.x && y >= rect.y &&
           static_cast<int64_t>(x) < right &&
           static_cast<int64_t>(y) < bottom;
}

bool fill_rect(const Surface &surface, Rect rect, uint32_t rgb) noexcept {
    if (!valid_surface(surface)) return false;

    int64_t left = rect.x;
    int64_t top = rect.y;
    int64_t right = left + rect.width;
    int64_t bottom = top + rect.height;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > surface.width) right = surface.width;
    if (bottom > surface.height) bottom = surface.height;
    if (left >= right || top >= bottom) return true;

    for (size_t y = static_cast<size_t>(top); y < static_cast<size_t>(bottom); ++y) {
        const size_t row = y * surface.stride;
        for (size_t x = static_cast<size_t>(left); x < static_cast<size_t>(right); ++x) {
            surface.pixels[row + x] = rgb;
        }
    }
    return true;
}

bool draw_text(const Surface &surface, int32_t x, int32_t y,
               const char *text, size_t length, uint32_t rgb,
               uint32_t scale) noexcept {
    if (!valid_surface(surface) || (text == nullptr && length != 0) ||
        length > 80 || scale == 0 || scale > 4) return false;

    for (size_t index = 0; index < length; ++index) {
        const uint8_t *rows = glyph(text[index]);
        const int64_t origin_x = static_cast<int64_t>(x) +
                                 static_cast<int64_t>(index) * 6 * scale;
        for (uint32_t row = 0; row < 7; ++row) {
            for (uint32_t col = 0; col < 5; ++col) {
                if ((rows[row] & (1u << (4 - col))) == 0) continue;
                for (uint32_t dy = 0; dy < scale; ++dy) {
                    const int64_t py = static_cast<int64_t>(y) + row * scale + dy;
                    if (py < 0 || py >= surface.height) continue;
                    for (uint32_t dx = 0; dx < scale; ++dx) {
                        const int64_t px = origin_x + col * scale + dx;
                        if (px < 0 || px >= surface.width) continue;
                        surface.pixels[static_cast<size_t>(py) * surface.stride +
                                       static_cast<size_t>(px)] = rgb;
                    }
                }
            }
        }
    }
    return true;
}

} // namespace jani::ui
