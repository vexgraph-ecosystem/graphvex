#include "image.h"

#include <stdlib.h>
#include <string.h>

// graphvex R3 — image.c
// Agnostic RGBA8 buffer with a CPU shadow. No GPU handle required.

struct Image {
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t usage;
    uint8_t *pixels;       // CPU shadow (RGBA8)
    void *native;          // opaque dialect handle
    void *ioSurface;       // opaque IOSurfaceRef
    uint32_t layer;        // atlas layer / sampler index
};

static void clamp_dims(uint32_t *w, uint32_t *h) {
    if (*w == 0) *w = 1;
    if (*h == 0) *h = 1;
}

Image *Image_new(const ImageDesc *desc) {
    Image *img = calloc(1, sizeof *img);
    if (!img) return NULL;
    img->width = desc ? desc->width : 1;
    img->height = desc ? desc->height : 1;
    img->format = desc ? desc->format : IMAGE_FORMAT_RGBA8;
    img->usage = desc ? desc->usage : IMAGE_USAGE_NONE;
    clamp_dims(&img->width, &img->height);
    return img;
}

Image *Image_0(void) { return Image_new(NULL); }
Image *Image_2(uint32_t width, uint32_t height) {
    ImageDesc d = {width, height, IMAGE_FORMAT_RGBA8, IMAGE_USAGE_NONE};
    return Image_new(&d);
}
Image *Image_4(uint32_t width, uint32_t height, uint32_t format, uint32_t usage) {
    ImageDesc d = {width, height, format, usage};
    return Image_new(&d);
}

void Image_destroy(Image *image) {
    if (!image) return;
    free(image->pixels);
    free(image);
}

bool Image_ensureShadow(Image *image, uint32_t width, uint32_t height) {
    if (!image) return false;
    clamp_dims(&width, &height);
    if (image->pixels && image->width == width && image->height == height) return true;
    uint8_t *grown = realloc(image->pixels, (size_t)width * height * 4u);
    if (!grown) return false;
    memset(grown, 0, (size_t)width * height * 4u);
    image->pixels = grown;
    image->width = width;
    image->height = height;
    return true;
}

bool Image_resize(Image *image, uint32_t width, uint32_t height) {
    return Image_ensureShadow(image, width, height);
}

bool Image_upload(const uint8_t *rgba, uint32_t width, uint32_t height, Image *dest) {
    if (!dest || !rgba) return false;
    if (!Image_ensureShadow(dest, width, height)) return false;
    memcpy(dest->pixels, rgba, (size_t)width * height * 4u);
    return true;
}

void Image_fill(Image *image, Color color) {
    if (!image) return;
    if (!image->pixels && !Image_ensureShadow(image, image->width, image->height)) return;
    uint8_t r = (uint8_t)Color_red(color);
    uint8_t g = (uint8_t)Color_green(color);
    uint8_t b = (uint8_t)Color_blue(color);
    uint8_t a = (uint8_t)Color_alpha(color);
    uint8_t *p = image->pixels;
    size_t n = (size_t)image->width * image->height;
    for (size_t i = 0; i < n; i++) {
        p[0] = r; p[1] = g; p[2] = b; p[3] = a;
        p += 4;
    }
}

uint32_t Image_width(const Image *image) { return image ? image->width : 0u; }
uint32_t Image_height(const Image *image) { return image ? image->height : 0u; }
uint32_t Image_format(const Image *image) { return image ? image->format : IMAGE_FORMAT_RGBA8; }
uint32_t Image_usage(const Image *image) { return image ? image->usage : IMAGE_USAGE_NONE; }
size_t Image_stride(const Image *image) { return image ? (size_t)image->width * 4u : 0u; }
uint8_t *Image_pixels(const Image *image) { return image ? image->pixels : NULL; }
bool Image_isValid(const Image *image) { return image && image->width > 0 && image->height > 0; }

void *Image_native(const Image *image) { return image ? image->native : NULL; }
void *Image_iosurface(const Image *image) { return image ? image->ioSurface : NULL; }
void Image_setNative(Image *image, void *native) { if (image) image->native = native; }
void Image_setIOSurface(Image *image, void *ioSurface) { if (image) image->ioSurface = ioSurface; }
uint32_t Image_layer(const Image *image) { return image ? image->layer : 0u; }
void Image_setLayer(Image *image, uint32_t layer) { if (image) image->layer = layer; }
