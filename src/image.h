#ifndef GRAPHICS_IMAGE_H
#define GRAPHICS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/graphics.h"   // Color

// graphvex R3 — image.h
//
// An RGBA8 pixel buffer. The substrate for scenes, offscreen Boards, and
// pixel-buffered panels. Owns a CPU shadow (so it works headless and is fully
// testable) plus an optional opaque native handle (VkImage / MTLTexture) that
// the backend fills in. IOSurface sharing is the macOS zero-copy path.
//
// Strict 0xRRGGBBAA Color Law: bytes in memory are R,G,B,A; the Metal drawable
// wants BGRA, so the conversion lives ONLY at the presentation boundary.

#define IMAGE_FORMAT_RGBA8 0u
#define IMAGE_FORMAT_BGRA8 1u

#define IMAGE_USAGE_NONE     0u
#define IMAGE_USAGE_SAMPLED  (1u << 0)
#define IMAGE_USAGE_RENDER   (1u << 1)
#define IMAGE_USAGE_TRANSFER (1u << 2)

typedef struct Image Image;

typedef struct ImageDesc {
    uint32_t width;    // default 1
    uint32_t height;   // default 1
    uint32_t format;   // IMAGE_FORMAT_* (default RGBA8)
    uint32_t usage;    // IMAGE_USAGE_* (default NONE)
} ImageDesc;

Image *Image_0(void);
Image *Image_2(uint32_t width, uint32_t height);
Image *Image_4(uint32_t width, uint32_t height, uint32_t format, uint32_t usage);
Image *Image_new(const ImageDesc *desc);
void Image_destroy(Image *image);

bool Image_resize(Image *image, uint32_t width, uint32_t height);
bool Image_ensureShadow(Image *image, uint32_t width, uint32_t height);
bool Image_upload(const uint8_t *rgba, uint32_t width, uint32_t height, Image *dest);
void Image_fill(Image *image, Color color);   // CPU shadow fill

uint32_t Image_width(const Image *image);
uint32_t Image_height(const Image *image);
uint32_t Image_format(const Image *image);
uint32_t Image_usage(const Image *image);
size_t   Image_stride(const Image *image);      // bytes per row
uint8_t *Image_pixels(const Image *image);      // CPU shadow (nullable)
bool     Image_isValid(const Image *image);

void  *Image_native(const Image *image);        // dialect handle (VkImage / MTLTexture)
void  *Image_iosurface(const Image *image);     // IOSurfaceRef when shared
void   Image_setNative(Image *image, void *native);
void   Image_setIOSurface(Image *image, void *ioSurface);
uint32_t Image_layer(const Image *image);       // atlas layer / sampler index (0 default)
void     Image_setLayer(Image *image, uint32_t layer);

#endif // GRAPHICS_IMAGE_H
