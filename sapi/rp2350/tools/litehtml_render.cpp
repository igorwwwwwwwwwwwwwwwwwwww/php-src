#include <litehtml.h>
#include "litehtml_cairo/container_cairo_pango.h"
#include "litehtml_cairo/cairo_images_cache.h"
#include <cairo.h>
#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <cstdlib>
#include <cstring>

namespace fs = std::filesystem;

static std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string url_decode(const std::string& src) {
    std::string out;
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '%' && i + 2 < src.size()) {
            unsigned int v = 0;
            sscanf(src.substr(i + 1, 2).c_str(), "%x", &v);
            out.push_back((char) v);
            i += 2;
        } else {
            out.push_back(src[i]);
        }
    }
    return out;
}

static std::string resolve_path(const std::string& url, const std::string& base_path) {
    if (url.empty()) return url;
    if (url.rfind("https://www.php.net/", 0) == 0) {
        return (fs::path(base_path) / url.substr(strlen("https://www.php.net/"))).string();
    }
    if (url.rfind("http://www.php.net/", 0) == 0) {
        return (fs::path(base_path) / url.substr(strlen("http://www.php.net/"))).string();
    }
    if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0) return url;
    if (url[0] == '/') {
        return (fs::path(base_path) / url.substr(1)).string();
    }
    return (fs::path(base_path) / url).string();
}

static bool run_command(const std::string& cmd) {
    int rc = std::system(cmd.c_str());
    return rc == 0;
}

static std::string ensure_png_for_image(const std::string& path) {
    std::string lower = path;
    for (char& c : lower) c = (char)std::tolower((unsigned char)c);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".svg") {
        std::string out = path + ".png";
        if (!fs::exists(out)) {
            std::string cmd = "rsvg-convert " + std::string("-o ") + '"' + out + '"' + " " + '"' + path + '"';
            run_command(cmd);
        }
        if (fs::exists(out)) {
            return out;
        }
    }
    return path;
}

struct SimpleContainer : public container_cairo_pango {
    std::string base_path;
    cairo_images_cache images;
    int screen_width;
    int screen_height;

    SimpleContainer(std::string base, int w, int h) : base_path(std::move(base)), screen_width(w), screen_height(h) {}

    void load_image(const char* src, const char* baseurl, bool) override {
        litehtml::size sz;
        get_image_size(src, baseurl, sz);
    }
    void set_caption(const char*) override {}
    void on_anchor_click(const char*, const litehtml::element::ptr&) override {}
    void on_mouse_event(const litehtml::element::ptr&, litehtml::mouse_event) override {}
    void set_cursor(const char*) override {}
    void set_base_url(const char* url) override { if (url) base_path = url; }
    const char* get_default_font_name() const override { return "sans-serif"; }
    cairo_font_options_t* get_font_options() override { return nullptr; }
    double get_screen_dpi() const override { return 96.0; }
    int get_screen_width() const override { return screen_width; }
    int get_screen_height() const override { return screen_height; }

    void get_viewport(litehtml::position& viewport) const override {
        viewport.x = 0; viewport.y = 0; viewport.width = screen_width; viewport.height = screen_height;
    }

    void make_url(const char* url, const char* baseurl, std::string& out) override {
        std::string u = url_decode(url ? url : "");
        std::string base = (baseurl && *baseurl) ? std::string(baseurl) : base_path;
        if (base.rfind("https://www.php.net/", 0) == 0 || base.rfind("http://www.php.net/", 0) == 0) {
            base = base_path;
        }
        if (u.rfind("https://www.php.net/", 0) == 0 || u.rfind("http://www.php.net/", 0) == 0) {
            out = resolve_path(u, base_path);
            return;
        }
        if (u.rfind("/", 0) == 0) {
            out = (fs::path(base_path) / u.substr(1)).string();
            return;
        }
        out = resolve_path(u, base);
    }

    void import_css(std::string& text, const std::string& url, std::string& baseurl) override {
        std::string path;
        make_url(url.c_str(), baseurl.c_str(), path);
        text = read_file(path);
        if (!path.empty()) {
            baseurl = base_path;
        }
    }

    void get_image_size(const char* src, const char* baseurl, litehtml::size& sz) override {
        std::string path;
        make_url(src, baseurl, path);
        path = ensure_png_for_image(path);
        int w = 0, h = 0, n = 0;
        if (!stbi_info(path.c_str(), &w, &h, &n)) {
            sz.width = 0;
            sz.height = 0;
            return;
        }
        sz.width = w;
        sz.height = h;
    }

    cairo_surface_t* get_image(const std::string& raw_url) override {
        std::string url = raw_url;
        if (url.empty()) return nullptr;
        cairo_surface_t* surf = images.get_image(url);
        if (surf) return surf;

        std::string path = ensure_png_for_image(url);
        int sw = 0, sh = 0, sch = 0;
        unsigned char* src = stbi_load(path.c_str(), &sw, &sh, &sch, 4);
        if (!src) return nullptr;

        surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sw, sh);
        unsigned char* dst = cairo_image_surface_get_data(surf);
        int dstride = cairo_image_surface_get_stride(surf);
        for (int y = 0; y < sh; y++) {
            unsigned char* srow = src + y * sw * 4;
            unsigned char* drow = dst + y * dstride;
            for (int x = 0; x < sw; x++) {
                unsigned char r = srow[x * 4 + 0];
                unsigned char g = srow[x * 4 + 1];
                unsigned char b = srow[x * 4 + 2];
                unsigned char a = srow[x * 4 + 3];
                drow[x * 4 + 0] = (unsigned char)((b * a) / 255);
                drow[x * 4 + 1] = (unsigned char)((g * a) / 255);
                drow[x * 4 + 2] = (unsigned char)((r * a) / 255);
                drow[x * 4 + 3] = a;
            }
        }
        cairo_surface_mark_dirty(surf);
        stbi_image_free(src);
        images.add_image(url, surf);
        return cairo_surface_reference(surf);
    }
};

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: litehtml_render <html-file> <out.png> [width] [height] [virtual-width] [virtual-height]\n";
        return 1;
    }
    std::string html_file = argv[1];
    std::string out_file = argv[2];
    int width = argc > 3 ? std::max(64, atoi(argv[3])) : 320;
    int height = argc > 4 ? std::max(64, atoi(argv[4])) : 240;
    int virtual_width = argc > 5 ? std::max(width, atoi(argv[5])) : width;
    int virtual_height = argc > 6 ? std::max(height, atoi(argv[6])) : height;
    std::string html = read_file(html_file);
    std::string base = fs::path(html_file).parent_path().string();
    SimpleContainer cont(base, virtual_width, virtual_height);
    auto doc = litehtml::document::createFromString(html, &cont);
    doc->render(virtual_width);

    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, virtual_width, virtual_height);
    cairo_t* cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    litehtml::position clip(0, 0, virtual_width, virtual_height);
    doc->draw((litehtml::uint_ptr)cr, 0, 0, &clip);
    cairo_destroy(cr);

    cairo_surface_t* out_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* out_cr = cairo_create(out_surface);
    cairo_set_source_rgb(out_cr, 1, 1, 1);
    cairo_paint(out_cr);
    double scale = std::min((double)width / (double)virtual_width, (double)height / (double)virtual_height);
    double dx = ((double)width - (double)virtual_width * scale) / 2.0;
    double dy = ((double)height - (double)virtual_height * scale) / 2.0;
    cairo_translate(out_cr, dx, dy);
    cairo_scale(out_cr, scale, scale);
    cairo_set_source_surface(out_cr, surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(out_cr), CAIRO_FILTER_BILINEAR);
    cairo_paint(out_cr);

    cairo_surface_write_to_png(out_surface, out_file.c_str());
    cairo_destroy(out_cr);
    cairo_surface_destroy(out_surface);
    cairo_surface_destroy(surface);
    std::cout << "wrote " << out_file << "\n";
    return 0;
}
