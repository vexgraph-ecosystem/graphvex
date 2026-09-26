#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "raster/raster_graphics.h"

enum { WIDTH = 11, HEIGHT = 9 };

// The original per-pixel rectangle algorithm, kept as an independent oracle.
static void referenceFill(uint8_t *pixels, const Rectangle *rect,
                          const Rectangle *clip, uint32_t color, float opacity) {
    float r = (float) ((color >> 24) & 255u) / 255.0f;
    float g = (float) ((color >> 16) & 255u) / 255.0f;
    float b = (float) ((color >> 8) & 255u) / 255.0f;
    float a = (float) (color & 255u) / 255.0f * opacity;
    float left = clip ? fmaxf(0.0f, (*clip).x) : 0.0f;
    float top = clip ? fmaxf(0.0f, (*clip).y) : 0.0f;
    float right = clip ? fminf((float) WIDTH, (*clip).x + (*clip).width) : (float) WIDTH;
    float bottom = clip ? fminf((float) HEIGHT, (*clip).y + (*clip).height) : (float) HEIGHT;
    int x0 = (int) fminf((float) WIDTH, fmaxf(left, floorf((*rect).x + 0.5f)));
    int y0 = (int) fminf((float) HEIGHT, fmaxf(top, floorf((*rect).y + 0.5f)));
    int x1 = (int) fmaxf(0.0f, fminf(right, floorf((*rect).x + (*rect).width + 0.5f)));
    int y1 = (int) fmaxf(0.0f, fminf(bottom, floorf((*rect).y + (*rect).height + 0.5f)));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT || a <= 0.0f)
                continue;
            if (clip && ((float) x < (*clip).x || (float) y < (*clip).y
                || (float) x >= (*clip).x + (*clip).width
                || (float) y >= (*clip).y + (*clip).height))
                continue;
            uint8_t *p = pixels + ((size_t) y * WIDTH + (size_t) x) * 4u;
            float da = (float) p[3] / 255.0f;
            p[0] = (uint8_t) ((r * a + ((float) p[0] / 255.0f) * (1.0f - a)) * 255.0f + 0.5f);
            p[1] = (uint8_t) ((g * a + ((float) p[1] / 255.0f) * (1.0f - a)) * 255.0f + 0.5f);
            p[2] = (uint8_t) ((b * a + ((float) p[2] / 255.0f) * (1.0f - a)) * 255.0f + 0.5f);
            p[3] = (uint8_t) ((a + da * (1.0f - a)) * 255.0f + 0.5f);
        }
    }
}

static void checkFill(const Graphics *row, const Rectangle *rect,
                      const Rectangle *clip, uint32_t color, float opacity) {
    uint8_t seed[WIDTH * HEIGHT * 4];
    uint8_t expected[sizeof(seed)];
    for (size_t i = 0; i < sizeof(seed); i++)
        seed[i] = (uint8_t) ((i * 73u + 31u) & 255u);
    memcpy(expected, seed, sizeof(seed));
    Image *fb = RasterGraphics_getFramebuffer();
    assert(Image_upload(seed, WIDTH, HEIGHT, fb));
    assert((*row).clip(clip));
    Brush brush = { color, opacity };
    referenceFill(expected, rect, clip, color, opacity);
    assert((*row).fillRect(rect, &brush));
    assert(memcmp(expected, Image_pixels(fb), sizeof(expected)) == 0);
}

int main(void) {
    const Graphics *row = RasterGraphics_getRow();
    assert(RasterGraphics_resize(WIDTH, HEIGHT));
    Rectangle whole = { 0, 0, WIDTH, HEIGHT };
    Rectangle partial = { -2.6f, 1.4f, 10.2f, 7.0f };
    Rectangle fractional = { 2.25f, 1.75f, 5.5f, 5.5f };
    Rectangle clip = { 3.25f, 2.75f, 4.4f, 3.5f };
    Rectangle outside = { -50, -50, 100, 100 };
    Rectangle empty = { 3, 3, -2, -2 };
    checkFill(row, &whole, nullptr, 0x2563B7FFu, 1.0f);
    checkFill(row, &partial, nullptr, 0xA20D41FFu, 1.0f);
    checkFill(row, &fractional, &clip, 0xCB277AFFu, 1.0f);
    checkFill(row, &outside, &clip, 0x4085D6FFu, 1.0f);
    checkFill(row, &empty, &clip, 0x122334FFu, 1.0f);
    checkFill(row, &partial, &clip, 0x11D4A9FFu, 0.5f);
    checkFill(row, &whole, nullptr, 0x98765480u, 1.0f);
    checkFill(row, &whole, nullptr, 0x12345600u, 1.0f);
    RasterGraphics_shutdown();
    puts("PASS raster_opaque_rect_test");
    return 0;
}
