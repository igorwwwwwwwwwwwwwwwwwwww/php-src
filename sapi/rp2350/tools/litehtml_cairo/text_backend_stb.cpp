#include "text_backend_stb.h"
#include "container_cairo.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb/stb_truetype.h"
#include <cairo.h>
#include <fstream>
#include <iterator>
#include <algorithm>

static std::vector<unsigned char> read_bytes_local(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void stb_text_backend::load_font_face(stb_font_face& face, const char* path) {
    face.file = read_bytes_local(path);
    face.info = std::make_unique<stbtt_fontinfo>();
    if (!face.file.empty()) {
        face.ok = stbtt_InitFont(face.info.get(), face.file.data(), stbtt_GetFontOffsetForIndex(face.file.data(), 0)) != 0;
    }
}

stb_text_backend::stb_text_backend() {
    load_font_face(m_regular, "/System/Library/Fonts/Supplemental/Arial.ttf");
    load_font_face(m_bold, "/opt/homebrew/lib/python3.9/site-packages/matplotlib/mpl-data/fonts/ttf/DejaVuSans-Bold.ttf");
}

litehtml::uint_ptr stb_text_backend::create_font(const litehtml::font_description& descr,
                                                 const litehtml::document*,
                                                 litehtml::font_metrics* fm) {
    auto fi = std::make_unique<stb_font_inst>();
    fi->face = (descr.weight >= 500 && m_bold.ok) ? &m_bold : &m_regular;
    fi->px = (int)(descr.size > 0 ? descr.size : 16);
    fi->weight = descr.weight;
    fi->underline = (descr.decoration_line & litehtml::text_decoration_line_underline) != 0;
    fi->strikeout = (descr.decoration_line & litehtml::text_decoration_line_line_through) != 0;
    fi->overline = (descr.decoration_line & litehtml::text_decoration_line_overline) != 0;
    fi->decoration_color = descr.decoration_color;
    if (fi->face && fi->face->ok) {
        fi->scale = stbtt_ScaleForPixelHeight(fi->face->info.get(), (float)fi->px);
        stbtt_GetFontVMetrics(fi->face->info.get(), &fi->ascent, &fi->descent, &fi->line_gap);
    }
    if (fm) {
        fm->font_size = fi->px;
        fm->ascent = (int)(fi->ascent * fi->scale);
        fm->height = (int)((fi->ascent - fi->descent + fi->line_gap) * fi->scale);
        fm->descent = (litehtml::pixel_t)std::max(0.0f, fm->height - fm->ascent);
        fm->x_height = std::max(1, (int)(fi->px * 0.5f));
        fm->ch_width = std::max(1, (int)(fi->px * 0.55f));
        fm->draw_spaces = true;
        fm->sub_shift = fi->px / 3;
        fm->super_shift = fi->px / 3;
    }
    auto* ret = fi.get();
    m_fonts.push_back(std::move(fi));
    return (litehtml::uint_ptr) ret;
}

void stb_text_backend::delete_font(litehtml::uint_ptr) {
}

litehtml::pixel_t stb_text_backend::text_width(const char* text, litehtml::uint_ptr hFont) {
    auto* fi = (stb_font_inst*) hFont;
    if (!fi || !fi->face || !fi->face->ok) return 0;
    int width = 0;
    int prev = 0;
    for (const unsigned char* p = (const unsigned char*) text; *p; ++p) {
        int cp = *p;
        if (cp & 0x80) continue;
        width += (int)(stbtt_GetCodepointKernAdvance(fi->face->info.get(), prev, cp) * fi->scale);
        int ax = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(fi->face->info.get(), cp, &ax, &lsb);
        width += (int)(ax * fi->scale);
        prev = cp;
    }
    return width;
}

void stb_text_backend::blend_text_to_argb32(uint32_t* dst,
                                           int dst_w,
                                           int dst_h,
                                           int dst_stride_pixels,
                                           const char* text,
                                           litehtml::uint_ptr hFont,
                                           litehtml::web_color color,
                                           const litehtml::position& pos) {
    auto* fi = (stb_font_inst*) hFont;
    if (!fi || !fi->face || !fi->face->ok || !dst) return;
    int x = (int)pos.x;
    int baseline = (int)pos.y + (int)(fi->ascent * fi->scale);
    int prev = 0;
    auto blend_glyph = [&](unsigned char* bmp, int w, int h, int dx0, int dy0) {
        for (int yy = 0; yy < h; yy++) {
            int dy = dy0 + yy;
            if (dy < 0 || dy >= dst_h) continue;
            uint32_t* row = dst + dy * dst_stride_pixels;
            for (int xx = 0; xx < w; xx++) {
                int dx = dx0 + xx;
                if (dx < 0 || dx >= dst_w) continue;
                unsigned char a = bmp[yy * w + xx];
                if (!a) continue;
                uint32_t& px = row[dx];
                unsigned char pb = (unsigned char)(px & 0xff);
                unsigned char pg = (unsigned char)((px >> 8) & 0xff);
                unsigned char pr = (unsigned char)((px >> 16) & 0xff);
                unsigned char pa = (unsigned char)((px >> 24) & 0xff);
                unsigned char nb = (unsigned char)((color.blue * a + pb * (255 - a)) / 255);
                unsigned char ng = (unsigned char)((color.green * a + pg * (255 - a)) / 255);
                unsigned char nr = (unsigned char)((color.red * a + pr * (255 - a)) / 255);
                unsigned char na = (unsigned char)std::min(255, (int)pa + (int)a);
                px = ((uint32_t)na << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
            }
        }
    };
    for (const unsigned char* p = (const unsigned char*) text; *p; ++p) {
        int cp = *p;
        if (cp & 0x80) continue;
        x += (int)(stbtt_GetCodepointKernAdvance(fi->face->info.get(), prev, cp) * fi->scale);
        int ax = 0, lsb = 0, w = 0, h = 0, xoff = 0, yoff = 0;
        stbtt_GetCodepointHMetrics(fi->face->info.get(), cp, &ax, &lsb);
        unsigned char* bmp = stbtt_GetCodepointBitmap(fi->face->info.get(), 0, fi->scale, cp, &w, &h, &xoff, &yoff);
        if (bmp) {
            blend_glyph(bmp, w, h, x + xoff, baseline + yoff);
            if (fi->weight >= 500) {
                blend_glyph(bmp, w, h, x + xoff + 1, baseline + yoff);
            }
            stbtt_FreeBitmap(bmp, nullptr);
        }
        x += (int)(ax * fi->scale);
        prev = cp;
    }
}

void stb_text_backend::draw_text(litehtml::uint_ptr hdc,
                                 const char* text,
                                 litehtml::uint_ptr hFont,
                                 litehtml::web_color color,
                                 const litehtml::position& pos) {
    auto* fi = (stb_font_inst*) hFont;
    auto* cr = (cairo_t*) hdc;
    cairo_surface_t* target = cairo_get_target(cr);
    cairo_surface_flush(target);
    auto* data = reinterpret_cast<uint32_t*>(cairo_image_surface_get_data(target));
    int stride_pixels = cairo_image_surface_get_stride(target) / 4;
    int surf_w = cairo_image_surface_get_width(target);
    int surf_h = cairo_image_surface_get_height(target);
    blend_text_to_argb32(data, surf_w, surf_h, stride_pixels, text, hFont, color, pos);
    cairo_surface_mark_dirty(target);

    litehtml::web_color deco = color;
    if (!fi->decoration_color.is_current_color) {
        deco = fi->decoration_color;
    }
    cairo_set_source_rgba(cr,
                          (double)deco.red / 255.0,
                          (double)deco.green / 255.0,
                          (double)deco.blue / 255.0,
                          (double)deco.alpha / 255.0);
    int tw = (int)text_width(text, hFont);
    if (fi->underline) {
        cairo_rectangle(cr, pos.x, pos.y + pos.height - 2, tw, 1);
        cairo_fill(cr);
    }
    if (fi->strikeout) {
        cairo_rectangle(cr, pos.x, pos.y + pos.height / 2, tw, 1);
        cairo_fill(cr);
    }
    if (fi->overline) {
        cairo_rectangle(cr, pos.x, pos.y + 1, tw, 1);
        cairo_fill(cr);
    }
}
