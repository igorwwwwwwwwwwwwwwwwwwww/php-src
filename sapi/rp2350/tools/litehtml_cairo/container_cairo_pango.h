#ifndef LITEBROWSER_CONTAINER_CAIRO_PANGO_H
#define LITEBROWSER_CONTAINER_CAIRO_PANGO_H

#include <litehtml.h>
#include "container_cairo.h"
#include "text_backend_pango.h"
#include <cairo.h>
#include <memory>

class container_cairo_pango : public container_cairo
{
	std::unique_ptr<litehtml_text_backend> m_backend;
public:
	container_cairo_pango();
	~container_cairo_pango() override;
	litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override;
	void delete_font(litehtml::uint_ptr hFont) override;
	litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override;
	void draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override;

	virtual cairo_font_options_t* get_font_options() { return nullptr; }
};

#endif //LITEBROWSER_CONTAINER_CAIRO_PANGO_H
