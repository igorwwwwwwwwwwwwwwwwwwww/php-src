#ifndef RP2350_LITEHTML_TEXT_BACKEND_STB_H
#define RP2350_LITEHTML_TEXT_BACKEND_STB_H

#include "text_backend.h"
#include <vector>
#include <memory>
#include "../third_party/stb/stb_truetype.h"

struct stb_font_face {
    std::vector<unsigned char> file;
    std::unique_ptr<stbtt_fontinfo> info;
    bool ok = false;
};

struct stb_font_inst {
    stb_font_face* face = nullptr;
    int px = 16;
    int weight = 400;
    float scale = 1.0f;
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    bool underline = false;
    bool strikeout = false;
    bool overline = false;
    litehtml::web_color decoration_color;
};

class stb_text_backend : public litehtml_text_backend {
    stb_font_face m_regular;
    stb_font_face m_bold;
    std::vector<std::unique_ptr<stb_font_inst>> m_fonts;
public:
    stb_text_backend();
    ~stb_text_backend() override = default;

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
private:
    static void load_font_face(stb_font_face& face, const char* path);
};

#endif
