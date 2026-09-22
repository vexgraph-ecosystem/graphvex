#ifndef LANG_IMAGE_H
#define LANG_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "c23/constructor.h"

// lang/image.h — the image contract (the language's pixel currency).
//
// An Image is the universal texture/image primitive every dialect shares: a
// native-pixel extent, a format, and the backend handles it carries (a VkImage,
// an id<MTLTexture>, an ID3D12Resource, a WGPUTexture). It also owns an OPTIONAL
// CPU shadow (`pixels`, width*height*4 RGBA8) — the reference surface the raster
// dialect and headless tests read, and the CPU path every filter can run.
//
// NO ROW: an Image is a RESOURCE, created BY a device, not a strategy with
// swappable implementations. So there is no ImageRow — a device hands one out
// and the same Image type carries whatever dialect handle it was born with.
//
// BYTE ORDER: RGBA8 — byte 0 = RED, 1 = GREEN, 2 = BLUE, 3 = ALPHA — the
// monotonic memory layout of the Strict 0xRRGGBBAA Color Law.
//
// Ported from the old reference (image/image.h), split to the lang/ contract
// per the Single Class Per File Law.

// Pixel formats (backend-agnostic codes; 0 = RGBA8 default).
#define IMAGE_FORMAT_RGBA8 0u
#define IMAGE_FORMAT_BGRA8 1u

// Usage flags (backend-agnostic; 0 = none).
#define IMAGE_USAGE_NONE      0u
#define IMAGE_USAGE_SAMPLED   (1u << 0)
#define IMAGE_USAGE_RENDER    (1u << 1)
#define IMAGE_USAGE_TRANSFER  (1u << 2)

// Opaque image: dialect-private handles live behind this.
typedef struct Image Image;

// Construction parameters. Every field has a default; zero it for a 1x1 RGBA8.
typedef struct ImageDesc {
    uint32_t width;    // pixels across (default 1)
    uint32_t height;   // pixels down (default 1)
    uint32_t format;   // IMAGE_FORMAT_* (default RGBA8)
    uint32_t usage;    // IMAGE_USAGE_* (default NONE)
} ImageDesc;

// --- Constructors (the arity-overloaded chooser idiom) ---
//
//   Image()                       -> 1x1 RGBA8 placeholder
//   Image(w, h)                   -> sized RGBA8
//   Image(w, h, format, usage)    -> fully specified
//   Image_new(&(ImageDesc){ ... })-> every other field
//
// Zeroed pixels; nullptr on zero dims or OOM (the Cold-Strict, Hot-Minimal
// Validation Law: fail closed).
Image *Image_0(void);
Image *Image_2(uint32_t width, uint32_t height);
Image *Image_4(uint32_t width, uint32_t height, uint32_t format, uint32_t usage);
Image *Image_new(const ImageDesc *desc);

#define IMAGE_CHOOSER(_0, _1, _2, _3, _4, NAME, ...) NAME
#define Image(...) IMAGE_CHOOSER( \
    dummy __VA_OPT__(,) __VA_ARGS__, \
    Image_4, Image_3, Image_2, Image_1, Image_0 \
)(__VA_ARGS__)

// Release the owned shadow and the struct. Null-safe no-op.
void Image_destroy(Image *image);

// --- Core functions ---
// Copy width*height*4 RGBA8 bytes into the shadow (dest-last); grows/clears on
// dim change. Returns false on null/shape mismatch (never crashes).
bool Image_upload(const uint8_t *rgba, uint32_t width, uint32_t height, Image *dest);

// Grow/clear the CPU shadow to the given extent (dest-last). Used by filters
// that need a scratch surface. Returns false on null/zero.
bool Image_ensureShadow(uint32_t width, uint32_t height, Image *dest);

// --- Getters (the Symmetric Getter/Setter Completeness Law: null-safe) ---
uint32_t Image_width(const Image *image);
uint32_t Image_height(const Image *image);
uint32_t Image_format(const Image *image);
uint32_t Image_usage(const Image *image);
uint8_t *Image_pixels(const Image *image);   // CPU shadow (nullable)
void    *Image_native(const Image *image);   // dialect handle, opaque transit
void    *Image_iosurface(const Image *image); // IOSurfaceRef when shared (nullable)

// --- Setters (dialect handle attach; opaque transit) ---
void Image_setNative(Image *image, void *native);
void Image_setIOSurface(Image *image, void *ioSurface);

#endif // LANG_IMAGE_H
