#ifndef GRAPHICS_IMAGE_H
#define GRAPHICS_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/graphics.h"

// graphvex R3 — image.h
//
// An RGBA8 pixel buffer. The substrate for scenes, offscreen Boards, and
// pixel-buffered panels. Owns an optional CPU shadow and/or a retained completed
// sampled GPU texture. GPU-only filter outputs remain drawable without readback.
// Legacy native/IOSurface fields are borrowed handles, not ownership references.
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
typedef bool (*ImageGpuRefFn)(void *resource);

typedef struct ImageDesc {
    uint32_t width;    // default 1
    uint32_t height;   // default 1
    uint32_t format;   // IMAGE_FORMAT_* (default RGBA8)
    uint32_t usage;    // IMAGE_USAGE_* (default NONE)
} ImageDesc;

Image *Image_0(void);
Image *Image_2(uint32_t width, uint32_t height);
Image *Image_4(uint32_t width, uint32_t height, uint32_t format, uint32_t usage);
#define GRAPHVEX_IMAGE_CTOR(_0,_1,_2,_3,_4,NAME,...) NAME
#define Image(...) GRAPHVEX_IMAGE_CTOR(0 __VA_OPT__(,) __VA_ARGS__, \
    Image_4,Image_invalidArity,Image_2,Image_invalidArity,Image_0)(__VA_ARGS__)
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

/* Owned reference to an optional immutable sampled texture; CPU pixels may be
 * absent. Driver admission validates matching extent and readiness; clearGpu unbinds.
 * Texture Device must outlive Image and any recorded GPU frame. Raw CPU writes
 * must call Image_clearGpu(image) to invalidate a prepared texture;
 * upload/fill/resize do so automatically. Image_destroy retains the object if
 * a final texture release cannot prove upload completion within 100ms. */
/* Backend admission supplies validated ready handles and paired callbacks.
 * This generic ownership seam does not force raster clients to link Vulkan. */
bool Image_bindGpu(Image *image, void *resource, void *device, void *descriptor,
                   ImageGpuRefFn retain, ImageGpuRefFn release);
bool Image_clearGpu(Image *image);
void *Image_gpuResource(const Image *image);
void *Image_gpuDevice(const Image *image);
void *Image_gpuDescriptor(const Image *image);
bool Image_isDrawable(const Image *image);

#endif // GRAPHICS_IMAGE_H
