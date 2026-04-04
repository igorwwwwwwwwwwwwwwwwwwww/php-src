#include <pixman.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    const int w = argc > 1 ? std::max(64, atoi(argv[1])) : 320;
    const int h = argc > 2 ? std::max(64, atoi(argv[2])) : 240;
    std::vector<unsigned int> dst((size_t)w * h, 0xffffffffu);
    std::vector<unsigned int> src((size_t)w * h, 0x00000000u);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            unsigned a = (unsigned)((x * 255) / (w - 1));
            unsigned r = 0x33;
            unsigned g = 0x66;
            unsigned b = 0xcc;
            src[(size_t)y * w + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    pixman_image_t* dst_img = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, reinterpret_cast<uint32_t*>(dst.data()), w * 4);
    pixman_image_t* src_img = pixman_image_create_bits(PIXMAN_a8r8g8b8, w, h, reinterpret_cast<uint32_t*>(src.data()), w * 4);
    pixman_image_composite32(PIXMAN_OP_OVER, src_img, nullptr, dst_img, 0, 0, 0, 0, 0, 0, w, h);
    pixman_image_unref(src_img);
    pixman_image_unref(dst_img);

    FILE* f = fopen("/tmp/pixman-smoke.ppm", "wb");
    if (!f) return 1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (size_t i = 0; i < dst.size(); i++) {
        unsigned v = dst[i];
        unsigned char rgb[3] = {
            (unsigned char)((v >> 16) & 0xff),
            (unsigned char)((v >> 8) & 0xff),
            (unsigned char)(v & 0xff),
        };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
    return 0;
}
