#ifndef RP2350_LITEHTML_TEXT_BACKEND_H
#define RP2350_LITEHTML_TEXT_BACKEND_H

#include <litehtml.h>
#include <cairo.h>
#include <string>

struct cairo_font;

class litehtml_text_backend {
public:
    virtual ~litehtml_text_backend() = default;

    virtual litehtml::uint_ptr create_font(const litehtml::font_description& descr,
                                           const litehtml::document* doc,
                                           litehtml::font_metrics* fm) = 0;
    virtual void delete_font(litehtml::uint_ptr hFont) = 0;
    virtual litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) = 0;
    virtual void draw_text(litehtml::uint_ptr hdc,
                           const char* text,
                           litehtml::uint_ptr hFont,
                           litehtml::web_color color,
                           const litehtml::position& pos) = 0;
};

#endif
