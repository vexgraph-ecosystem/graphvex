#include <assert.h>
#include <stdio.h>
#include "lang/graphics.h"
#include "raster/raster_graphics.h"
int main(void) {
    assert(Graphics_registerRow(RasterGraphics_getRow()));
    assert(Graphics_setGraphics(LANG_BACKEND_RASTER));
    assert(Graphics_resize(40, 20));
    assert(Graphics_clear(0x000000FFu));
    Rectangle clip = { 10, 3, 10, 8 }, bounds = {0, 0, 40, 20}, saved;
    Brush ink = { 0xFFFFFFFFu, 0.5f };
    assert(Graphics_clip(&clip));
    assert(Graphics_getClip(&saved));
    assert(Rectangle_equals(&clip, &saved));
    assert(Graphics_drawText(&bounds, "MMMMM\nMMMMM", &ink));
    Image *fb = RasterGraphics_getFramebuffer();
    uint8_t *pixels = Image_pixels(fb);
    unsigned painted = 0;
    for (unsigned y = 0; y < 20; y++) {
        for (unsigned x = 0; x < 40; x++) {
            unsigned char red = pixels[(y * 40 + x) * 4];
            if (x < 10 || x >= 20 || y < 3 || y >= 11)
                assert(red == 0);
            if (red) {
                assert(red == 128);
                painted++;
            }
        }
    }
    assert(painted);
    Image *image = Image_2(4, 2);
    uint8_t rgba[4 * 2 * 4];
    for (size_t i = 0; i < sizeof(rgba); i++)
        rgba[i] = 255;
    assert(Image_upload(rgba, 4, 2, image));
    Rectangle destination = {0, 0, 30, 20};
    assert(Graphics_drawImageFit(image, &destination, IMAGE_FIT_COVER, IMAGE_ANCHOR_CENTER, 0, 0, nullptr));
    assert(Graphics_getClip(&saved) && Rectangle_equals(&clip, &saved));
    assert(pixels[0] == 0);
    Image_destroy(image);
    assert(Graphics_clip(nullptr));
    assert(!Graphics_getClip(&saved));
    RasterGraphics_shutdown();
    puts("PASS text_clip_test: text respects scissor and opacity");
}
