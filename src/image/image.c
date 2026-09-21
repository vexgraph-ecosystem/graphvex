#include "image/image.h"

#include <stdlib.h>
#include <string.h>

#include "nio/mem.h"
#include "oop/type.h"
#include "graphics/graphics.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Image
 * ============================================================================
 * Unified graphics image primitive serving across all rendering pipelines
 * according to the Unified Graphics System Law. Supports Vulkan (VkImage),
 * Metal (MTLTexture / IOSurface), Direct3D 12 (ID3D12Resource*), and CPU-side
 * RGBA8 fallback buffers.
 *
 * Automatically resolves the active GPU handle via Image_getGpuHandle based on
 * the currently selected backend in Graphics (graphics/graphics.h).
 *
 * Each image instance maintains an owned CPU-side RGBA8 shadow buffer sized to
 * exactly width * height * 4 bytes (byte 0 = RED, 1 = GREEN, 2 = BLUE,
 * 3 = ALPHA — the monotonic memory layout of the Strict 0xRRGGBBAA Color Law).
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Image (image/image.c)
 * LEVEL: L2 — Behavior (GPU image behavior API, unified graphics handles)
 * ============================================================================
 * SUMMARY:
 *   Unified GPU image primitive. Manages backend handles (VkImage, MTLTexture,
 *   D3D12, IOSurface) alongside an owned CPU RGBA8 shadow buffer.
 *
 * STRUCT FIELDS (Mirroring image/image.h):
 * ----------------------------------------------------------------------------
 *   uint32_t width;        // pixels across (>= 1)
 *   uint32_t height;       // pixels down (>= 1)
 *   uint32_t format;       // backend-agnostic pixel format code (0 = RGBA8 stub default)
 *   uint32_t usage;        // backend-agnostic usage flags (0 = none)
 *   uint64_t typeId;       // block-header type id (TYPE_IMAGE_SINGLETON)
 *   uint8_t *rgba;         // OWNED CPU shadow, width*height*4 bytes RGBA8
 *   void    *vkImage;      // VkImage handle (Vulkan backend)
 *   void    *metalTexture; // id<MTLTexture> handle (Metal backend)
 *   void    *d3d12Resource;// ID3D12Resource* handle (DirectX 12 backend)
 *   void    *ioSurface;    // IOSurfaceRef handle (zero-copy shared GPU memory)
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Image_0(void)
 *   - Image_2(w, h)
 *   - Image_4(w, h, format, usage)
 *
 * Private Constructors: (.c static)
 *   - imageCreate(w, h, format, usage)
 *
 * Public Core Functions: (.h)
 *   - Image_free(img)
 *   - Image_upload(rgba, w, h, dest)
 *   - Image_getGpuHandle(img)
 *
 * Private Core Functions: (.c static)
 *   - imageByteCount(w, h, outBytes)
 *   - imageFreeStorage(img)
 *   - imageResize(img, w, h)
 *
 * Public Setters: (.h)
 *   - Image_setWidth(img, w)
 *   - Image_setHeight(img, h)
 *   - Image_setFormat(img, format)
 *   - Image_setUsage(img, usage)
 *   - Image_setVkImage(img, vkImage)
 *   - Image_setMetalTexture(img, metalTexture)
 *   - Image_setDirect12Resource(img, d3d12Resource)
 *   - Image_setIOSurface(img, ioSurface)
 *
 * Public Getters: (.h)
 *   - Image_getWidth(img)
 *   - Image_getHeight(img)
 *   - Image_getFormat(img)
 *   - Image_getUsage(img)
 *   - Image_getVkImage(img)
 *   - Image_getMetalTexture(img)
 *   - Image_getDirect12Resource(img)
 *   - Image_getIOSurface(img)
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
        return nullptr;
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
    (*img).vkImage = nullptr;
    (*img).metalTexture = nullptr;
    (*img).d3d12Resource = nullptr;
    (*img).ioSurface = nullptr;
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
    size_t n = (size_t) w * (size_t) h;
    if (w != 0 && n / (size_t) w != (size_t) h)
        return false;
    if (n > SIZE_MAX / 4)
        return false;
    *outBytes = n * 4;
    return true;
}

static void imageFreeStorage(Image *img) {
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
    memcpy((*dest).rgba, rgba, (size_t) w * (size_t) h * 4);
    return true;
}

void *Image_getGpuHandle(const Image *img) {
    if (!img)
        return nullptr;
    uint32_t backend = Graphics_getGraphicsId();
    switch (backend) {
        case GRAPHICS_BACKEND_VULKAN:
            return (*img).vkImage;
        case GRAPHICS_BACKEND_METAL:
            return (*img).metalTexture;
        default:
            return (*img).rgba;
    }
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

;;SETTER
void Image_setVkImage(Image *img, void *vkImage) {
    if (!img)
        return;
    (*img).vkImage = vkImage;
}

;;SETTER
void Image_setMetalTexture(Image *img, void *metalTexture) {
    if (!img)
        return;
    (*img).metalTexture = metalTexture;
}

;;SETTER
void Image_setDirect12Resource(Image *img, void *d3d12Resource) {
    if (!img)
        return;
    (*img).d3d12Resource = d3d12Resource;
}

;;SETTER
void Image_setIOSurface(Image *img, void *ioSurface) {
    if (!img)
        return;
    (*img).ioSurface = ioSurface;
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

;;GETTER
void *Image_getVkImage(const Image *img) {
    return img ? (*img).vkImage : nullptr;
}

;;GETTER
void *Image_getMetalTexture(const Image *img) {
    return img ? (*img).metalTexture : nullptr;
}

;;GETTER
void *Image_getDirect12Resource(const Image *img) {
    return img ? (*img).d3d12Resource : nullptr;
}

;;GETTER
void *Image_getIOSurface(const Image *img) {
    return img ? (*img).ioSurface : nullptr;
}
