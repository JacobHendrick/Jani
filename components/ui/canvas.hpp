#ifndef JANI_COMPONENTS_UI_CANVAS_HPP
#define JANI_COMPONENTS_UI_CANVAS_HPP

#include <stddef.h>
#include <stdint.h>

namespace jani::ui {

struct Surface {
    uint32_t *pixels;
    size_t pixel_count;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct Rect {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
};

bool valid_surface(const Surface &surface) noexcept;
bool fill_rect(const Surface &surface, Rect rect, uint32_t rgb) noexcept;
bool draw_text(const Surface &surface, int32_t x, int32_t y,
               const char *text, size_t length, uint32_t rgb,
               uint32_t scale) noexcept;
bool contains_point(Rect rect, int32_t x, int32_t y) noexcept;

} // namespace jani::ui

#endif
