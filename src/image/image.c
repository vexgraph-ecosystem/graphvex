#include "image/image.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Image
 * ============================================================================
 * Backend-agnostic two-dimensional discrete pixel memory container serving as
 * the unified graphics image primitive across all rendering layers. Succeeds
 * legacy bindless textures and platform-specific surface bridges by decoupling
 * host CPU image allocations from downstream graphics device driver specifics.
 *
 * Each image instance maintains an owned CPU-side alpha-first ARGB8 shadow buffer sized to
 * exactly width * height * 4 bytes (byte 0 = ALPHA, 1 = RED, 2 = GREEN,
 * 3 = BLUE — mirroring the 0xAARRGGBB stored word per the Strict Color Law).
 * Struct memory allocations are serviced via
 * the vexspoke typed memory arena (TYPE_IMAGE_SINGLETON) — the single
 * allocation source; the arena falls back to a private malloc block when its
 * bump region is full, and Image_free reclaims (or safely declines) through
 * Memory_free, never a raw free of the struct. The pixel
 * payload buffer is strictly owned by the Image handle and released upon destruction.
 * In accordance with the Unified Graphics Abstraction Law, coordinate systems,
 * formats, and dimensions remain backend-neutral and zero-tolerant.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Image (image/image.c)
 * LEVEL: L2 — Behavior (GPU image behavior API, CPU-shadow stub)
 * ============================================================================
 * SUMMARY:
 *   Backend-agnostic GPU image primitive and successor to legacy bindless
 *   registries and platform-specific surface bridges. Maintains an owned
 *   CPU-side alpha-first ARGB8 shadow buffer (width * height * 4 bytes) zero-initialized
 *   to transparent black until bound to a hardware rasterization pipeline.
 *
 * STRUCT FIELDS (Mirroring image/image.h):
 * ----------------------------------------------------------------------------
 *   uint32_t width;   // pixels across (>= 1)
 *   uint32_t height;  // pixels down (>= 1)
 *   uint32_t format;  // backend-agnostic pixel format code (0 = ARGB8 stub default)
 *   uint32_t usage;   // backend-agnostic usage flags (0 = none)
 *   uint64_t typeId;  // block-header type id (TYPE_IMAGE_SINGLETON)
 *   uint8_t *rgba;    // OWNED CPU shadow, width*height*4 bytes alpha-first ARGB8 (null = no backing); freed by Image_free, never borrowed
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Image_0(void)                                      : 1x1 placeholder image
 *   - Image_2(w, h)                                      : Sized image with default format and usage
 *   - Image_4(w, h, format, usage)                       : Fully specified image instance
 *
 * Private Constructors: (.c static)
 *   - imageCreate(w, h, format, usage)                   : Internal allocator and shadow initializer
 *
 * Public Core Functions: (.h)
 *   - Image_free(img)                                    : Release image shadow and struct memory
 *   - Image_upload(rgba, w, h, dest)                     : Copy alpha-first ARGB8 pixels into destination shadow
 *
 * Private Core Functions: (.c static)
 *   - imageByteCount(w, h, outBytes)                     : Validate dimensions and compute byte size
 *   - imageFreeStorage(img)                              : Deallocate arena or heap struct block
 *   - imageResize(img, w, h)                             : Reallocate owned shadow buffer to new dims
 *
 * Public Setters: (.h)
 *   - Image_setWidth(img, w)                             : Mutate image width and resize buffer
 *   - Image_setHeight(img, h)                            : Mutate image height and resize buffer
 *   - Image_setFormat(img, format)                       : Mutate image pixel format code
 *   - Image_setUsage(img, usage)                         : Mutate image backend usage flags
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Image_getWidth(img)                                : Query image pixel width
 *   - Image_getHeight(img)                               : Query image pixel height
 *   - Image_getFormat(img)                               : Query image pixel format code
 *   - Image_getUsage(img)                                : Query image backend usage flags
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// CONSTRUCTORS (PUBLIC & PRIVATE)

static bool imageByteCount(uint32_t w, uint32_t h, size_t *outBytes);
static void imageFreeStorage(Image *img);
static void imageResize(Image *img, uint32_t w, uint32_t h);

static Image *imageCreate(uint32_t w, uint32_t h, uint32_t format, uint32_t usage) {
    if (w == 0 || h == 0)
        return nullptr;
    size_t bytes = 0;
    if (!imageByteCount(w, h, &bytes))
        return nullptr;
    Image *img = (Image*) Memory_alloc(TYPE_IMAGE_SINGLETON, sizeof(Image));
    if (!img)
        return nullptr; // single allocation source: the arena. Memory_alloc
                        // itself falls back to a private malloc block when the
                        // bump region is full, and Memory_free reclaims (or
                        // safely declines) accordingly — see imageFreeStorage.
    uint8_t *pixels = (uint8_t*) calloc(bytes, 1);
    if (!pixels) {
        imageFreeStorage(img);
        return nullptr;
    }
    (*img).width = w;
    (*img).height = h;
    (*img).format = format;
    (*img).usage = usage;
    (*img).typeId = TYPE_IMAGE_SINGLETON;
    (*img).rgba = pixels;
    return img;
}

Image *Image_0(void) {
    return imageCreate(1, 1, 0, 0);
}

Image *Image_2(uint32_t w, uint32_t h) {
    return imageCreate(w, h, 0, 0);
}

Image *Image_4(uint32_t w, uint32_t h, uint32_t format, uint32_t usage) {
    return imageCreate(w, h, format, usage);
}

// CORE FUNCTIONS (PUBLIC & PRIVATE)

static bool imageByteCount(uint32_t w, uint32_t h, size_t *outBytes) {
    if (!outBytes)
        return false;
    size_t n = (size_t)w * (size_t)h;
    if (w != 0 && n / (size_t)w != (size_t)h)
        return false;
    if (n > SIZE_MAX / 4)
        return false;
    *outBytes = n * 4;
    return true;
}

static void imageFreeStorage(Image *img) {
    // The struct is ALWAYS arena-managed (Memory_alloc): imageCreate has no
    // calloc fallback, so a raw free() can never be correct here. Memory_free
    // is safe on every block Memory_alloc can return — bump-resident, slab
    // slots, and the arena's own malloc-fallback blocks (which arena_free
    // deliberately declines to reclaim, an OOM-only leak by design). A raw
    // free() of an arena block would trip malloc's zone ownership checks.
    Memory_free(img);
}

static void imageResize(Image *img, uint32_t w, uint32_t h) {
    size_t bytes = 0;
    if (!imageByteCount(w, h, &bytes))
        return;
    uint8_t *pixels = (uint8_t*) calloc(bytes, 1);
    if (!pixels)
        return;
    free((*img).rgba);
    (*img).rgba = pixels;
    (*img).width = w;
    (*img).height = h;
}

void Image_free(Image *img) {
    if (!img)
        return;
    free((*img).rgba);
    (*img).rgba = nullptr;
    imageFreeStorage(img);
}

bool Image_upload(const uint8_t *rgba, uint32_t w, uint32_t h, Image *dest) {
    if (!rgba || !dest)
        return false;
    if (w == 0 || h == 0)
        return false;
    if (w != (*dest).width || h != (*dest).height || !(*dest).rgba)
        imageResize(dest, w, h);
    if (!(*dest).rgba || w != (*dest).width || h != (*dest).height)
        return false;
    memcpy((*dest).rgba, rgba, (size_t)w * (size_t)h * 4);
    return true;
}

// SETTERS (PUBLIC & PRIVATE)

;;SETTER
void Image_setWidth(Image *img, uint32_t w) {
    if (!img)
        return;
    if (w == 0 || w == (*img).width)
        return;
    imageResize(img, w, (*img).height);
}

;;SETTER
void Image_setHeight(Image *img, uint32_t h) {
    if (!img)
        return;
    if (h == 0 || h == (*img).height)
        return;
    imageResize(img, (*img).width, h);
}

;;SETTER
void Image_setFormat(Image *img, uint32_t format) {
    if (!img)
        return;
    (*img).format = format;
}

;;SETTER
void Image_setUsage(Image *img, uint32_t usage) {
    if (!img)
        return;
    (*img).usage = usage;
}

// GETTERS (PUBLIC & PRIVATE)

;;GETTER
uint32_t Image_getWidth(const Image *img) {
    return img ? (*img).width : 0;
}

;;GETTER
uint32_t Image_getHeight(const Image *img) {
    return img ? (*img).height : 0;
}

;;GETTER
uint32_t Image_getFormat(const Image *img) {
    return img ? (*img).format : 0;
}

;;GETTER
uint32_t Image_getUsage(const Image *img) {
    return img ? (*img).usage : 0;
}
