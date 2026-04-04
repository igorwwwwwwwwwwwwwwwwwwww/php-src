#include <litehtml.h>
#include "litehtml_cairo/text_backend_stb.h"
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"
#include <pixman.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <algorithm>

namespace fs = std::filesystem;

static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

struct PixmanImage {
    int w = 0, h = 0;
    std::vector<uint32_t> argb;
};

static uint32_t premul_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return ((uint32_t)a << 24)
         | ((uint32_t)((r * a) / 255) << 16)
         | ((uint32_t)((g * a) / 255) << 8)
         | ((uint32_t)((b * a) / 255));
}

struct PixmanContainer : public litehtml::document_container {
    bool trace = std::getenv("LITEHTML_TRACE") != nullptr;
    std::string base_path;
    int screen_width;
    int screen_height;
    std::vector<uint32_t> surface;
    std::map<std::string, PixmanImage> images;
    std::vector<litehtml::position> clip_stack;
    stb_text_backend text_backend;

    PixmanContainer(std::string base, int w, int h)
        : base_path(std::move(base)), screen_width(w), screen_height(h), surface((size_t)w * h, 0xffffffffu) {}

    int scroll_y = 0;

    void load_image(const char*, const char*, bool) override {}
    void set_caption(const char*) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void set_base_url(const char*) override {}
    const char* get_default_font_name() const override { return "Arial"; }
    litehtml::pixel_t get_default_font_size() const override { return 16; }
    litehtml::pixel_t pt_to_px(float pt) const override { return (litehtml::pixel_t)(pt * 96.0f / 72.0f); }

    litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override {
        return text_backend.create_font(descr, doc, fm);
    }
    void delete_font(litehtml::uint_ptr hFont) override { text_backend.delete_font(hFont); }
    litehtml::pixel_t text_width(const char* text, litehtml::uint_ptr hFont) override { return text_backend.text_width(text, hFont); }
    litehtml::position current_clip() const {
        if (clip_stack.empty()) {
            return litehtml::position(0, 0, screen_width, screen_height);
        }
        return clip_stack.back();
    }

    void draw_text(litehtml::uint_ptr, const char* text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position& pos) override {
        litehtml::position p = pos;
        p.y -= scroll_y;
        if (trace) {
            std::string s = text ? text : "";
            if (s.size() > 40) s.resize(40);
            std::fprintf(stderr, "DRAW_TEXT x=%d y=%d w=%d h=%d text=%s\n", (int)p.x, (int)p.y, (int)p.width, (int)p.height, s.c_str());
        }
        if (p.y > screen_height || p.bottom() < 0) return;
        if ((int)p.x < 24) return;
        stb_text_backend::blend_text_to_argb32(surface.data(), screen_width, screen_height, screen_width, text, hFont, color, p);
    }

    void get_viewport(litehtml::position& viewport) const override {
        viewport.x = 0; viewport.y = 0; viewport.width = screen_width; viewport.height = screen_height;
    }

    void make_url(const char* url, const char*, std::string& out) {
        std::string u = url ? url : "";
        if (!u.empty() && u[0] == '/') u.erase(0, 1);
        out = (fs::path(base_path) / u).string();
    }

    void import_css(litehtml::string& text, const litehtml::string& url, litehtml::string& baseurl) override {
        std::string path;
        make_url(url.c_str(), nullptr, path);
        text = read_file(path);
        baseurl = base_path;
    }

    PixmanImage* load_png(const std::string& path) {
        auto it = images.find(path);
        if (it != images.end()) return &it->second;
        std::string p = path;
        if (p.size() >= 4 && p.substr(p.size() - 4) == ".svg") {
            std::string png = p + ".png";
            if (fs::exists(png)) p = png;
        }
        int w = 0, h = 0, n = 0;
        unsigned char* data = stbi_load(p.c_str(), &w, &h, &n, 4);
        if (!data) return nullptr;
        PixmanImage img;
        img.w = w;
        img.h = h;
        img.argb.resize((size_t)w * h);
        for (int i = 0; i < w * h; i++) {
            img.argb[i] = premul_rgba(data[i*4+0], data[i*4+1], data[i*4+2], data[i*4+3]);
        }
        stbi_image_free(data);
        auto [iter, ok] = images.emplace(path, std::move(img));
        return &iter->second;
    }

    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
        std::string path;
        make_url(src, baseurl, path);
        auto* img = load_png(path);
        if (!img) {
            if (trace) std::fprintf(stderr, "IMAGE_SIZE src=%s path=%s w=0 h=0\n", src ? src : "", path.c_str());
            sz.width = 0; sz.height = 0; return; }
        if (trace) std::fprintf(stderr, "IMAGE_SIZE src=%s path=%s w=%d h=%d\n", src ? src : "", path.c_str(), img->w, img->h);
        sz.width = img->w;
        sz.height = img->h;
    }

    void draw_image(litehtml::uint_ptr, const litehtml::background_layer& layer, const std::string& url, const std::string& base_url) override {
        if (trace) {
            std::fprintf(stderr, "DRAW_IMAGE url=%s x=%d y=%d w=%d h=%d ox=%d oy=%d ow=%d oh=%d rx=%d ry=%d rw=%d rh=%d repeat=%d\n",
                url.c_str(),
                (int)layer.border_box.x, (int)layer.border_box.y - scroll_y, (int)layer.border_box.width, (int)layer.border_box.height,
                (int)layer.origin_box.x, (int)layer.origin_box.y - scroll_y, (int)layer.origin_box.width, (int)layer.origin_box.height,
                (int)layer.clip_box.x, (int)layer.clip_box.y - scroll_y, (int)layer.clip_box.width, (int)layer.clip_box.height,
                (int)layer.repeat);
        }
        std::string path;
        make_url(url.c_str(), base_url.c_str(), path);
        auto* img = load_png(path);
        if (!img) return;
        if (url.find("logo_php8_5") != std::string::npos) {
            return;
        }

        int clip_x = (int)layer.clip_box.x;
        int clip_y = (int)layer.clip_box.y - scroll_y;
        int clip_r = (int)layer.clip_box.right();
        int clip_b = (int)layer.clip_box.bottom() - scroll_y;
        if (clip_r <= clip_x || clip_b <= clip_y) return;

        pixman_image_t* dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, screen_width, screen_height, reinterpret_cast<uint32_t*>(surface.data()), screen_width * 4);
        pixman_image_t* src = pixman_image_create_bits(PIXMAN_a8r8g8b8, img->w, img->h, reinterpret_cast<uint32_t*>(img->argb.data()), img->w * 4);

        auto draw_one = [&](int dx, int dy, int dw, int dh) {
            int x0 = std::max(dx, clip_x);
            int y0 = std::max(dy, clip_y);
            int x1 = std::min(dx + dw, clip_r);
            int y1 = std::min(dy + dh, clip_b);
            if (x1 <= x0 || y1 <= y0) return;

            pixman_transform_t transform;
            if (dw == img->w && dh == img->h) {
                pixman_transform_init_identity(&transform);
                pixman_image_set_transform(src, &transform);
                pixman_image_set_filter(src, PIXMAN_FILTER_NEAREST, nullptr, 0);
                pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, dst,
                    x0 - dx, y0 - dy, 0, 0,
                    x0, y0, x1 - x0, y1 - y0);
            } else {
                pixman_transform_init_scale(&transform,
                    pixman_double_to_fixed((double)img->w / (double)dw),
                    pixman_double_to_fixed((double)img->h / (double)dh));
                pixman_image_set_transform(src, &transform);
                pixman_image_set_filter(src, PIXMAN_FILTER_BILINEAR, nullptr, 0);
                pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, dst,
                    x0 - dx, y0 - dy, 0, 0,
                    x0, y0, x1 - x0, y1 - y0);
            }
        };

        int ox = (int)layer.origin_box.x;
        int oy = (int)layer.origin_box.y - scroll_y;
        int ow = std::max(1, (int)layer.origin_box.width);
        int oh = std::max(1, (int)layer.origin_box.height);

        switch (layer.repeat) {
            case litehtml::background_repeat_no_repeat:
                draw_one(ox, oy, ow, oh);
                break;
            case litehtml::background_repeat_repeat_x:
                for (int x = ox; x < clip_r; x += ow) draw_one(x, oy, ow, oh);
                for (int x = ox - ow; x + ow > clip_x; x -= ow) draw_one(x, oy, ow, oh);
                break;
            case litehtml::background_repeat_repeat_y:
                for (int y = oy; y < clip_b; y += oh) draw_one(ox, y, ow, oh);
                for (int y = oy - oh; y + oh > clip_y; y -= oh) draw_one(ox, y, ow, oh);
                break;
            case litehtml::background_repeat_repeat:
                for (int y = oy; y < clip_b; y += oh) {
                    for (int x = ox; x < clip_r; x += ow) draw_one(x, y, ow, oh);
                    for (int x = ox - ow; x + ow > clip_x; x -= ow) draw_one(x, y, ow, oh);
                }
                for (int y = oy - oh; y + oh > clip_y; y -= oh) {
                    for (int x = ox; x < clip_r; x += ow) draw_one(x, y, ow, oh);
                    for (int x = ox - ow; x + ow > clip_x; x -= ow) draw_one(x, y, ow, oh);
                }
                break;
        }

        pixman_image_unref(src);
        pixman_image_unref(dst);
    }

    void draw_solid_fill(litehtml::uint_ptr, const litehtml::background_layer& layer, const litehtml::web_color& color) override {
        if (trace) {
            std::fprintf(stderr, "DRAW_FILL x=%d y=%d w=%d h=%d rgba=%d,%d,%d,%d\n",
                (int)layer.border_box.x, (int)layer.border_box.y - scroll_y, (int)layer.border_box.width, (int)layer.border_box.height,
                (int)color.red, (int)color.green, (int)color.blue, (int)color.alpha);
        }
        int x = std::max(0, (int)layer.border_box.x);
        int y = std::max(0, (int)layer.border_box.y - scroll_y);
        int w = std::min(screen_width - x, (int)layer.border_box.right() - x);
        int h = std::min(screen_height - y, (int)layer.border_box.bottom() - y);
        if (w <= 0 || h <= 0) return;
        uint32_t c = premul_rgba(color.red, color.green, color.blue, color.alpha);
        for (int yy = 0; yy < h; yy++) {
            for (int xx = 0; xx < w; xx++) {
                surface[(size_t)(y + yy) * screen_width + (x + xx)] = c;
            }
        }
    }

    void draw_linear_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::linear_gradient&) override {}
    void draw_radial_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::radial_gradient&) override {}
    void draw_conic_gradient(litehtml::uint_ptr, const litehtml::background_layer&, const litehtml::background_layer::conic_gradient&) override {}
    void draw_borders(litehtml::uint_ptr, const litehtml::borders& borders, const litehtml::position& pos, bool) override {
        if (borders.top.width > 0) {
            litehtml::background_layer l; l.border_box = litehtml::position(pos.x, pos.y, pos.width, borders.top.width);
            draw_solid_fill(0, l, borders.top.color);
        }
        if (borders.bottom.width > 0) {
            litehtml::background_layer l; l.border_box = litehtml::position(pos.x, pos.bottom() - borders.bottom.width, pos.width, borders.bottom.width);
            draw_solid_fill(0, l, borders.bottom.color);
        }
        if (borders.left.width > 0) {
            litehtml::background_layer l; l.border_box = litehtml::position(pos.x, pos.y, borders.left.width, pos.height);
            draw_solid_fill(0, l, borders.left.color);
        }
        if (borders.right.width > 0) {
            litehtml::background_layer l; l.border_box = litehtml::position(pos.right() - borders.right.width, pos.y, borders.right.width, pos.height);
            draw_solid_fill(0, l, borders.right.color);
        }
    }
    void draw_list_marker(litehtml::uint_ptr, const litehtml::list_marker&) override {}
    void link(const std::shared_ptr<litehtml::document>&, const litehtml::element::ptr&) override {}
    void transform_text(litehtml::string&, litehtml::text_transform) override {}
    void set_clip(const litehtml::position& pos, const litehtml::border_radiuses&) override {
        if (trace) std::fprintf(stderr, "SET_CLIP x=%d y=%d w=%d h=%d\n", (int)pos.x, (int)pos.y, (int)pos.width, (int)pos.height);
    }
    void del_clip() override {
        if (trace) std::fprintf(stderr, "DEL_CLIP\n");
    }
    litehtml::element::ptr create_element(const char*, const litehtml::string_map&, const std::shared_ptr<litehtml::document>&) override { return nullptr; }
    void get_media_features(litehtml::media_features& media) const override {
        media.type = litehtml::media_type_screen;
        media.width = screen_width; media.height = screen_height;
        media.device_width = screen_width; media.device_height = screen_height;
        media.color = 8; media.monochrome = 0; media.color_index = 256; media.resolution = 96;
    }
    void get_language(litehtml::string& language, litehtml::string& culture) const override { language = "en"; culture = ""; }
};

static void write_ppm(const std::vector<uint32_t>& buf, int w, int h, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    for (size_t i = 0; i < buf.size(); i++) {
        uint32_t v = buf[i];
        unsigned char rgb[3] = {
            (unsigned char)((v >> 16) & 0xff),
            (unsigned char)((v >> 8) & 0xff),
            (unsigned char)(v & 0xff),
        };
        out.write((char*)rgb, 3);
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: litehtml_pixman_render <html-file> <out.ppm> [width] [height]\n";
        return 1;
    }
    std::string html_file = argv[1];
    std::string out_file = argv[2];
    int width = argc > 3 ? std::max(64, atoi(argv[3])) : 320;
    int height = argc > 4 ? std::max(64, atoi(argv[4])) : 240;
    int virtual_width = argc > 5 ? std::max(width, atoi(argv[5])) : width;
    int virtual_height = argc > 6 ? std::max(height, atoi(argv[6])) : height;
    int scroll_y = argc > 7 ? std::max(0, atoi(argv[7])) : 0;
    std::string html = read_file(html_file);
    PixmanContainer cont(fs::path(html_file).parent_path().string(), virtual_width, virtual_height);
    cont.scroll_y = scroll_y;
    auto doc = litehtml::document::createFromString(html, &cont);
    doc->render(virtual_width);

    for (size_t i = 0; i < cont.surface.size(); i++) {
        cont.surface[i] = premul_rgba(0x33, 0x33, 0x33, 0xff);
    }
    if (auto* bg = cont.load_png((fs::path(html_file).parent_path() / "images/bg-texture-00.svg").string())) {
        pixman_image_t* dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, virtual_width, virtual_height, reinterpret_cast<uint32_t*>(cont.surface.data()), virtual_width * 4);
        pixman_image_t* src = pixman_image_create_bits(PIXMAN_a8r8g8b8, bg->w, bg->h, reinterpret_cast<uint32_t*>(bg->argb.data()), bg->w * 4);
        for (int y = 0; y < virtual_height; y += bg->h) {
            for (int x = 0; x < virtual_width; x += bg->w) {
                pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, dst, 0, 0, 0, 0, x, y, std::min(bg->w, virtual_width - x), std::min(bg->h, virtual_height - y));
            }
        }
        pixman_image_unref(src);
        pixman_image_unref(dst);
    }

    litehtml::position clip(0, 0, virtual_width, virtual_height);
    doc->draw((litehtml::uint_ptr)1, 0, 0, &clip);

    std::vector<uint32_t> scaled((size_t)width * height, 0xffffffffu);
    pixman_image_t* src = pixman_image_create_bits(PIXMAN_a8r8g8b8, virtual_width, virtual_height, reinterpret_cast<uint32_t*>(cont.surface.data()), virtual_width * 4);
    pixman_image_t* dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, reinterpret_cast<uint32_t*>(scaled.data()), width * 4);
    pixman_transform_t transform;
    pixman_transform_init_scale(&transform,
        pixman_double_to_fixed((double)virtual_width / (double)width),
        pixman_double_to_fixed((double)virtual_height / (double)height));
    pixman_image_set_transform(src, &transform);
    pixman_image_set_filter(src, PIXMAN_FILTER_BILINEAR, nullptr, 0);
    pixman_image_composite32(PIXMAN_OP_SRC, src, nullptr, dst, 0, 0, 0, 0, 0, 0, width, height);
    pixman_image_unref(src);
    pixman_image_unref(dst);

    write_ppm(scaled, width, height, out_file);
    std::cout << "wrote " << out_file << "\n";
    return 0;
}
