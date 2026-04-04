#include "rp2350_vfs.h"
#include "rp2350_tft.h"
#include "litehtml.h"
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <cstdio>

static const rp2350_vfs_file_t* rp2350_vfs_find(const char* path) {
    for (size_t i = 0; i < rp2350_vfs_files_count; i++) {
        if (strcmp(rp2350_vfs_files[i].path, path) == 0) return &rp2350_vfs_files[i];
    }
    return nullptr;
}

static std::string rp2350_vfs_read(const std::string& path) {
    const rp2350_vfs_file_t* f = rp2350_vfs_find(path.c_str());
    if (!f) return {};
    return std::string(f->source, f->len);
}

static std::string dirname_of(const std::string& path) {
    size_t p = path.rfind('/');
    if (p == std::string::npos || p == 0) return "/";
    return path.substr(0, p);
}

static std::string join_url(const std::string& base, const std::string& url) {
    if (url.empty()) return base;
    if (url[0] == '/') return url;
    std::string dir = dirname_of(base);
    std::string full = dir + "/" + url;
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= full.size()) {
        size_t j = full.find('/', i);
        if (j == std::string::npos) j = full.size();
        std::string seg = full.substr(i, j - i);
        if (!seg.empty() && seg != ".") {
            if (seg == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(seg);
            }
        }
        i = j + 1;
        if (j == full.size()) break;
    }
    std::string out;
    for (auto& seg : parts) out += "/" + seg;
    return out.empty() ? "/" : out;
}

static uint16_t rgb565(litehtml::web_color c) {
    return (uint16_t)(((c.red & 0xF8) << 8) | ((c.green & 0xFC) << 3) | (c.blue >> 3));
}

class rp2350_container : public litehtml::document_container {
public:
    std::string base_path;

    litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document*, litehtml::font_metrics* fm) override {
        int scale = 1;
        if (descr.size >= 28) scale = 3;
        else if (descr.size >= 16) scale = 2;
        if (fm) {
            fm->ascent = 8 * scale;
            fm->descent = 2 * scale;
            fm->height = 10 * scale;
            fm->x_height = 8 * scale;
            fm->draw_spaces = true;
        }
        return (litehtml::uint_ptr) scale;
    }
    void delete_font(litehtml::uint_ptr) override {}
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override {
        int scale = (int) hFont;
        if (scale < 1) scale = 1;
        size_t len = text ? strlen(text) : 0;
        return (litehtml::pixel_t) (len * (6 * scale + scale));
    }
    void draw_text(litehtml::uint_ptr, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override {
        int scale = (int) hFont;
        if (scale < 1) scale = 1;
        rp2350_tft_fb_draw_text(pos.x, pos.y, text ? text : "", text ? strlen(text) : 0, rgb565(color), scale, 1);
    }
    litehtml::pixel_t pt_to_px(float pt) const override { return (litehtml::pixel_t) pt; }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    const char* get_default_font_name() const override { return "builtin"; }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override {}
    void load_image(const char*, const char*, bool) override {}
    void get_image_size(const char*, const char*, litehtml::size& sz) override { sz.width = 0; sz.height = 0; }
    void draw_image(litehtml::uint_ptr, const litehtml::background_layer&, const std::string&, const std::string&) override {}
    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& bg, const litehtml::web_color& color) override {
        rp2350_tft_fb_fill_rect(bg.clip_box.x, bg.clip_box.y, bg.clip_box.width, bg.clip_box.height, rgb565(color));
    }
    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::conic_gradient&) override {}
    void draw_borders(litehtml::uint_ptr, const litehtml::borders&, const litehtml::position&, bool) override {}
    void set_caption(const char*) override {}
    void set_base_url(const char* base_url) override { base_path = base_url ? base_url : ""; }
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void transform_text(std::string& text, litehtml::text_transform tt) override {
        if (tt == litehtml::text_transform_uppercase) {
            for (char& c : text) if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        }
    }
    void import_css(std::string& text, const std::string& url, std::string& baseurl) override {
        std::string path = join_url(base_path.empty() ? "/phpnet/www.php.net/index.html" : base_path, url);
        text = rp2350_vfs_read(path);
        baseurl = path;
    }
    void set_clip(const litehtml::position&, const litehtml::border_radiuses&) override {}
    void del_clip() override {}
    void get_viewport(litehtml::position& viewport) const override {
        viewport.x = 0; viewport.y = 0; viewport.width = 320; viewport.height = 240;
    }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&, const std::shared_ptr<litehtml::document>&) override { return nullptr; }
    void get_media_features(litehtml::media_features& media) const override {
        media.type = litehtml::media_type_screen;
        media.width = 320;
        media.height = 240;
        media.device_width = 320;
        media.device_height = 240;
        media.color = 16;
        media.monochrome = 0;
        media.color_index = 256;
        media.resolution = 96;
    }
    void get_language(std::string& language, std::string& culture) const override {
        language = "en";
        culture.clear();
    }
};

extern "C" int rp2350_litehtml_render_tft(void) {
    std::string path = "/phpnet/www.php.net/index.html";
    std::string html = rp2350_vfs_read(path);
    if (html.empty()) return -1;
    rp2350_container cont;
    cont.base_path = path;
    rp2350_tft_fb_clear(0x07E0);
    rp2350_tft_fb_fill_rect(0, 0, 32, 32, 0xF800);
    litehtml::estring input(html, litehtml::encoding::utf_8, litehtml::confidence::certain);
    auto doc = litehtml::document::createFromString(input, &cont, path);
    if (!doc) return -2;
    rp2350_tft_fb_fill_rect(32, 0, 32, 32, 0xFFE0);
    doc->render(320);
    rp2350_tft_fb_fill_rect(64, 0, 32, 32, 0x001F);
    litehtml::position clip(0, 0, 320, 240);
    doc->draw((litehtml::uint_ptr)1, 0, 0, &clip);
    rp2350_tft_fb_fill_rect(96, 0, 32, 32, 0xFFFF);
    rp2350_tft_fb_draw_text(8, 220, "litehtml", 8, 0xFFFF, 1, 1);
    rp2350_tft_fb_render(0, 0);
    return 0;
}
