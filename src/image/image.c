#include "lang/image.h"

#include <stdlib.h>
#include <string.h>

#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Image
 * ============================================================================
 * The language's pixel currency. An Image carries a native-pixel extent, a
 * format, an OPTIONAL CPU shadow (RGBA8, width*height*4), and the opaque
 * dialect handle it was born with (VkImage, id<MTLTexture>, ID3D12Resource,
 * WGPUTexture). It is a RESOURCE, not a strategy — there is no ImageRow; a
 * device hands one out and the same type travels every dialect.
 *
 * The CPU shadow is what makes Image useful headless: the raster dialect
 * presents it, headless tests read it, and every CPU filter (blur, contrast,
 * depth-of-field) runs on it. A GPU-only image may leave it null; every
 * accessor is null-safe (the Cold-Strict, Hot-Minimal Validation Law).
 *
 * Lifetime: the struct + shadow are heap-owned; destroy frees both. The
 * dialect handle is NOT freed here (the device owns it) — set it to null
 * before destroy, or let the device reap it first (the Teardown Order Law).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Image (image/image.c)
 * LEVEL: L2 — Behavior (backend-agnostic pixel resource)
 * ============================================================================
 * SUMMARY:
 *   Universal texture/image primitive with an optional CPU shadow. Ported
 *   from the old reference and reduced to the language contract: extent,
 *   format, usage, shadow, and opaque dialect handles.
 *
 * STRUCT FIELDS (Mirroring lang/image.h incomplete tag — completed here):
 * ----------------------------------------------------------------------------
 *   uint32_t width;      // pixels across (>= 1)
 *   uint32_t height;     // pixels down (>= 1)
 *   uint32_t format;     // IMAGE_FORMAT_* (0 = RGBA8)
 *   uint32_t usage;      // IMAGE_USAGE_* (0 = none)
 *   uint8_t *pixels;     // OWNED CPU shadow, width*height*4 RGBA8 (nullable)
 *   void *native;        // opaque dialect handle (VkImage / MTLTexture / ...)
 *   void *ioSurface;     // IOSurfaceRef when shared (nullable)
 *
 * PRIVATE HELPERS:
 * ----------------------------------------------------------------------------
 *   shadowAlloc(width, height) : allocate a zeroed RGBA8 shadow (or null)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Image_0(void) / Image_2(w,h) / Image_4(w,h,fmt,use) / Image_new(desc)
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Image_destroy(image)
 *   - Image_upload(rgba, w, h, dest)
 *   - Image_ensureShadow(w, h, dest)
 *
 * Private Core Functions: (.c static)
 *   - shadowAlloc(width, height)
 *
 * Public Setters: (.h)
 *   - Image_setNative(image, native)
 *   - Image_setIOSurface(image, ioSurface)
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Image_width(image) / Image_height(image)
 *   - Image_format(image) / Image_usage(image)
 *   - Image_pixels(image) / Image_native(image) / Image_iosurface(image)
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

struct Image {
    uint32_t width;      // pixels across (>= 1)
    uint32_t height;     // pixels down (>= 1)
    uint32_t format;     // IMAGE_FORMAT_* (0 = RGBA8)
    uint32_t usage;      // IMAGE_USAGE_* (0 = none)
    uint8_t *pixels;     // OWNED CPU shadow, width*height*4 RGBA8 (nullable)
    void *native;        // opaque dialect handle (VkImage / MTLTexture / ...)
    void *ioSurface;     // IOSurfaceRef when shared (nullable)
};

// CONSTRUCTORS (PUBLIC & PRIVATE)

// Allocate a zeroed RGBA8 shadow. Overflow-safe (the Cold-Strict, Hot-Minimal
// Validation Law): a w*h*4 that cannot fit size_t answers null, never wraps.
static uint8_t *shadowAlloc(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0)
        return nullptr;
    size_t bytes = (size_t) width * (size_t) height * 4u;
    if (bytes / 4u != (size_t) width * (size_t) height)
        return nullptr; // overflow
    return (uint8_t*) calloc(1, bytes);
}

Image *Image_new(const ImageDesc *desc) {
    uint32_t width = (desc != nullptr && (*desc).width > 0) ? (*desc).width : 1u;
    uint32_t height = (desc != nullptr && (*desc).height > 0) ? (*desc).height : 1u;
    uint32_t format = (desc != nullptr) ? (*desc).format : IMAGE_FORMAT_RGBA8;
    uint32_t usage = (desc != nullptr) ? (*desc).usage : IMAGE_USAGE_NONE;

    Image *image = (Image*) calloc(1, sizeof(Image));
    if (image == nullptr)
        return nullptr;
    (*image).width = width;
    (*image).height = height;
    (*image).format = format;
    (*image).usage = usage;
    (*image).pixels = shadowAlloc(width, height);
    if ((*image).pixels == nullptr) {
        free(image);
        return nullptr;
    }
    return image;
}

Image *Image_0(void) {
    return Image_new(nullptr);
}

Image *Image_2(uint32_t width, uint32_t height) {
    return Image_new(&(ImageDesc){ .width = width, .height = height });
}

Image *Image_4(uint32_t width, uint32_t height, uint32_t format, uint32_t usage) {
    return Image_new(&(ImageDesc){ .width = width, .height = height,
                                   .format = format, .usage = usage });
}

void Image_destroy(Image *image) {
    if (image == nullptr)
        return;
    free((*image).pixels);
    (*image).pixels = nullptr;
    free(image);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

bool Image_ensureShadow(uint32_t width, uint32_t height, Image *dest) {
    if (dest == nullptr || width == 0 || height == 0)
        return false;
    if ((*dest).pixels != nullptr && (*dest).width == width && (*dest).height == height)
        return true;
    uint8_t *fresh = shadowAlloc(width, height);
    if (fresh == nullptr)
        return false;
    free((*dest).pixels);
    (*dest).pixels = fresh;
    (*dest).width = width;
    (*dest).height = height;
    return true;
}

bool Image_upload(const uint8_t *rgba, uint32_t width, uint32_t height, Image *dest) {
    if (rgba == nullptr || dest == nullptr)
        return false;
    if (!Image_ensureShadow(width, height, dest))
        return false;
    memcpy((*dest).pixels, rgba, (size_t) width * (size_t) height * 4u);
    return true;
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Image_setNative(Image *image, void *native) {
    if (image == nullptr)
        return;
    (*image).native = native;
}

;;SETTER
void Image_setIOSurface(Image *image, void *ioSurface) {
    if (image == nullptr)
        return;
    (*image).ioSurface = ioSurface;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t Image_width(const Image *image) {
    return image ? (*image).width : 0u;
}

;;GETTER
uint32_t Image_height(const Image *image) {
    return image ? (*image).height : 0u;
}

;;GETTER
uint32_t Image_format(const Image *image) {
    return image ? (*image).format : 0u;
}

;;GETTER
uint32_t Image_usage(const Image *image) {
    return image ? (*image).usage : 0u;
}

;;GETTER
uint8_t *Image_pixels(const Image *image) {
    return image ? (*image).pixels : nullptr;
}

;;GETTER
void *Image_native(const Image *image) {
    return image ? (*image).native : nullptr;
}

;;GETTER
void *Image_iosurface(const Image *image) {
    return image ? (*image).ioSurface : nullptr;
}
