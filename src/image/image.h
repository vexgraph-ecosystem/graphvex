#ifndef IMAGE_IMAGE_H
#define IMAGE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "graphvex/type.h"

// image/image.h — Backend-agnostic GPU image, CPU-shadow stub.
//
// Successor to the Texture_* bindless registry and VkIOSurface_* bridge
// concepts. No Vulkan includes here: pixels live in an owned CPU shadow
// until a backend claims the handle.
//
// BYTE ORDER: RGBA8 (byte 0 = RED, 1 = GREEN, 2 = BLUE, 3 = ALPHA), the
// monotonic memory layout of the Strict 0xRRGGBBAA Color Law.
// The software row (raster_graphics) consumes bytes verbatim: byte 0 feeds
// channel 0 (red).

typedef struct Image {
    uint32_t width;   // pixels across (>= 1)
    uint32_t height;  // pixels down (>= 1)
    uint32_t format;  // backend-agnostic pixel format code (0 = RGBA8 stub default)
    uint32_t usage;   // backend-agnostic usage flags (0 = none)
    uint64_t typeId;  // block-header type id (TYPE_IMAGE_SINGLETON)
    uint8_t *rgba;    // OWNED CPU shadow, width*height*4 bytes RGBA8 (byte0=red..byte3=alpha per the Strict 0xRRGGBBAA Color Law; null = no backing); freed by Image_free, never borrowed
} Image;

// Empty 1x1 placeholder (format 0, usage 0, zeroed pixel)
Image *Image_0(void);

// Sized image (format 0, usage 0, zeroed pixels); null on zero dims or OOM
Image *Image_2(uint32_t w, uint32_t h);

// Fully specified image (zeroed pixels); null on zero dims or OOM
Image *Image_4(uint32_t w, uint32_t h, uint32_t format, uint32_t usage);

// Release the owned shadow then the struct (null-safe no-op)
void Image_free(Image *img);

// Copy w*h*4 RGBA8 bytes into dest (dest-last); grows/clears on dim change
bool Image_upload(const uint8_t *rgba, uint32_t w, uint32_t h, Image *dest);

// Symmetric mutators (width/height realloc the shadow, cleared to zero)
void Image_setWidth(Image *img, uint32_t w);
void Image_setHeight(Image *img, uint32_t h);
void Image_setFormat(Image *img, uint32_t format);
void Image_setUsage(Image *img, uint32_t usage);

// Null-safe inspectors (null yields 0)
uint32_t Image_getWidth(const Image *img);
uint32_t Image_getHeight(const Image *img);
uint32_t Image_getFormat(const Image *img);
uint32_t Image_getUsage(const Image *img);

#define Image(...) CONSTRUCTOR_DISPATCH(Image, __VA_ARGS__)
#endif
