#include "../../components/ui/canvas.hpp"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

using jani::ui::Rect;
using jani::ui::Surface;

int main() {
    uint32_t pixels[20];
    for (uint32_t &pixel : pixels) pixel = 0x112233;

    const Surface surface{pixels, 20, 4, 4, 5};
    assert(jani::ui::valid_surface(surface));
    assert(jani::ui::fill_rect(surface, Rect{-1, 1, 3, 2}, 0xaabbcc));
    assert(pixels[5] == 0xaabbcc && pixels[6] == 0xaabbcc);
    assert(pixels[10] == 0xaabbcc && pixels[11] == 0xaabbcc);
    assert(pixels[7] == 0x112233 && pixels[9] == 0x112233);
    assert(pixels[15] == 0x112233);

    assert(jani::ui::fill_rect(surface, Rect{INT32_MAX, INT32_MAX, UINT32_MAX, UINT32_MAX}, 0));
    assert(pixels[5] == 0xaabbcc);

    const Surface short_buffer{pixels, 19, 4, 4, 5};
    const Surface short_stride{pixels, 20, 4, 4, 3};
    const Surface null_buffer{nullptr, 20, 4, 4, 5};
    assert(!jani::ui::valid_surface(short_buffer));
    assert(!jani::ui::fill_rect(short_buffer, Rect{0, 0, 4, 4}, 0));
    assert(!jani::ui::draw_text(short_stride, 0, 0, "A", 1, 0, 1));
    assert(!jani::ui::valid_surface(null_buffer));
    assert(pixels[0] == 0x112233);

    uint32_t glyph_pixels[42] = {};
    const Surface glyph{glyph_pixels, 42, 6, 7, 6};
    assert(jani::ui::draw_text(glyph, 0, 0, "A", 1, 0xffffff, 1));
    assert(glyph_pixels[0] == 0 && glyph_pixels[1] == 0xffffff);
    assert(glyph_pixels[2] == 0xffffff && glyph_pixels[3] == 0xffffff);
    assert(glyph_pixels[3 * 6] == 0xffffff);
    assert(glyph_pixels[3 * 6 + 4] == 0xffffff);
    assert(glyph_pixels[5] == 0);

    assert(!jani::ui::draw_text(glyph, 0, 0, nullptr, 1, 0, 1));
    assert(!jani::ui::draw_text(glyph, 0, 0, "A", 1, 0, 0));
    assert(!jani::ui::draw_text(glyph, 0, 0, "A", 1, 0, 5));
    char too_long[81] = {};
    assert(!jani::ui::draw_text(glyph, 0, 0, too_long, 81, 0, 1));
    assert(jani::ui::draw_text(glyph, INT32_MAX, 0, "A", 1, 0, 1));
    assert(glyph_pixels[1] == 0xffffff);

    const Rect new_button{770, 180, 128, 43};
    assert(jani::ui::contains_point(new_button, 770, 180));
    assert(jani::ui::contains_point(new_button, 897, 222));
    assert(!jani::ui::contains_point(new_button, 898, 222));
    assert(!jani::ui::contains_point(new_button, 897, 223));
    assert(!jani::ui::contains_point(Rect{0, 0, 0, 10}, 0, 0));
    assert(jani::ui::contains_point(Rect{INT32_MAX - 1, 0, UINT32_MAX, 1},
                                    INT32_MAX, 0));

    puts("test_ui_canvas: passed");
    return 0;
}
