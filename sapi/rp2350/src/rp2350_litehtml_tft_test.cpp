#include "rp2350_tft.h"
#include "pico/stdlib.h"
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
int rp2350_litehtml_render_tft(void);
#ifdef __cplusplus
}
#endif

int main(void) {
    stdio_init_all();
    printf("[tft-test] boot\r\n");
    if (!rp2350_tft_init()) {
        printf("[tft-test] tft init fail\r\n");
        while (true) {}
    }
    printf("[tft-test] tft init ok\r\n");
    rp2350_tft_backlight(65535);
    printf("[tft-test] backlight on\r\n");
    rp2350_tft_clear(0xF800);
    printf("[tft-test] clear red\r\n");
    sleep_ms(250);
    rp2350_tft_clear(0xFFE0);
    printf("[tft-test] clear yellow\r\n");
    sleep_ms(250);
    rp2350_tft_clear(0x001F);
    printf("[tft-test] clear blue\r\n");
    sleep_ms(250);
    int rc = rp2350_litehtml_render_tft();
    printf("[tft-test] render rc=%d\r\n", rc);
    if (rc != 0) {
        if (rc == -1) {
            rp2350_tft_clear(0xF800);
        } else if (rc == -2) {
            rp2350_tft_clear(0xFFE0);
        } else {
            rp2350_tft_clear(0x07E0);
        }
    }
    uint16_t level = 65535;
    bool down = true;
    while (true) {
        rp2350_tft_backlight(level);
        sleep_ms(16);
        if (down) {
            if (level > 2048) level -= 2048; else down = false;
        } else {
            if (level < 63487) level += 2048; else down = true;
        }
    }
}
