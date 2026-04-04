#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb/stb_image.h"
#include <pixman.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <cstdint>

static uint32_t premul_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    return ((uint32_t)a << 24)
         | ((uint32_t)((r * a) / 255) << 16)
         | ((uint32_t)((g * a) / 255) << 8)
         | ((uint32_t)((b * a) / 255));
}

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
        std::cerr << "usage: pixman_image_debug <in.png> <out.ppm> [dw] [dh]\n";
        return 1;
    }
    std::string in = argv[1];
    std::string out = argv[2];
    int dw = argc > 3 ? std::max(1, atoi(argv[3])) : 320;
    int dh = argc > 4 ? std::max(1, atoi(argv[4])) : 240;

    int sw = 0, sh = 0, n = 0;
    unsigned char* data = stbi_load(in.c_str(), &sw, &sh, &n, 4);
    if (!data) {
        std::cerr << "load failed\n";
        return 2;
    }

    std::vector<uint32_t> srcbuf((size_t)sw * sh);
    for (int i = 0; i < sw * sh; i++) {
        srcbuf[i] = premul_rgba(data[i*4+0], data[i*4+1], data[i*4+2], data[i*4+3]);
    }
    stbi_image_free(data);

    std::vector<uint32_t> dstbuf((size_t)dw * dh, 0xffffffffu);
    pixman_image_t* src = pixman_image_create_bits(PIXMAN_a8r8g8b8, sw, sh, reinterpret_cast<uint32_t*>(srcbuf.data()), sw * 4);
    pixman_image_t* dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, dw, dh, reinterpret_cast<uint32_t*>(dstbuf.data()), dw * 4);

    pixman_transform_t transform;
    if (sw == dw && sh == dh) {
        pixman_transform_init_identity(&transform);
        pixman_image_set_transform(src, &transform);
        pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, dst, 0, 0, 0, 0, 0, 0, dw, dh);
    } else {
        pixman_transform_init_scale(&transform,
            pixman_double_to_fixed((double)sw / (double)dw),
            pixman_double_to_fixed((double)sh / (double)dh));
        pixman_image_set_transform(src, &transform);
        pixman_image_set_filter(src, PIXMAN_FILTER_BILINEAR, nullptr, 0);
        pixman_image_composite32(PIXMAN_OP_OVER, src, nullptr, dst, 0, 0, 0, 0, 0, 0, dw, dh);
    }

    pixman_image_unref(src);
    pixman_image_unref(dst);
    write_ppm(dstbuf, dw, dh, out);
    return 0;
}
