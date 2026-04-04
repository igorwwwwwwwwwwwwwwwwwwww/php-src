#include "container_cairo_pango.h"
#include "text_backend_stb.h"
#include <cstdlib>

container_cairo_pango::container_cairo_pango()
{
    const char* backend = std::getenv("LITEHTML_TEXT_BACKEND");
    if (backend && std::string(backend) == "stb") {
        m_backend = std::make_unique<stb_text_backend>();
    } else {
        m_backend = std::make_unique<pango_text_backend>();
    }
}

container_cairo_pango::~container_cairo_pango() = default;

litehtml::uint_ptr container_cairo_pango::create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics *fm)
{
    return m_backend->create_font(descr, doc, fm);
}

void container_cairo_pango::delete_font(litehtml::uint_ptr hFont)
{
    m_backend->delete_font(hFont);
}

litehtml::pixel_t container_cairo_pango::text_width(const char *text, litehtml::uint_ptr hFont)
{
    return m_backend->text_width(text, hFont);
}

void container_cairo_pango::draw_text(litehtml::uint_ptr hdc, const char *text, litehtml::uint_ptr hFont,
                                      litehtml::web_color color, const litehtml::position &pos)
{
    apply_clip((cairo_t*) hdc);
    m_backend->draw_text(hdc, text, hFont, color, pos);
}
