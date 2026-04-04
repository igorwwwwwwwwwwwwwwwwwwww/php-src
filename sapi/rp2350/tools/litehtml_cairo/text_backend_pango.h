#ifndef RP2350_LITEHTML_TEXT_BACKEND_PANGO_H
#define RP2350_LITEHTML_TEXT_BACKEND_PANGO_H

#include "text_backend.h"
#include <pango/pangocairo.h>
#include <pango/pango-font.h>
#include <set>

struct cairo_font
{
    PangoFontDescription* font;
    litehtml::pixel_t size;
    bool underline;
    bool strikeout;
    bool overline;
    litehtml::pixel_t ascent;
    litehtml::pixel_t descent;
    int underline_thickness;
    int underline_position;
    int strikethrough_thickness;
    int strikethrough_position;
    int overline_thickness;
    int overline_position;
    int decoration_style;
    litehtml::web_color decoration_color;
};

class pango_text_backend : public litehtml_text_backend {
    cairo_surface_t* m_temp_surface;
    cairo_t* m_temp_cr;
    std::set<std::string> m_all_fonts;
public:
    pango_text_backend();
    ~pango_text_backend() override;

    litehtml::uint_ptr create_font(const litehtml::font_description& descr,
                                   const litehtml::document* doc,
                                   litehtml::font_metrics* fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc,
                   const char* text,
                   litehtml::uint_ptr hFont,
                   litehtml::web_color color,
                   const litehtml::position& pos) override;
};

#endif
