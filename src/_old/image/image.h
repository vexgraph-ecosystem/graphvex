#ifndef IMAGE_IMAGE_H
#define IMAGE_IMAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"
#include "../graphics/type.h"

// image/image.h — Unified Graphics Image Primitive.
//
// Single Class Per File Law: Image.
//
// In accordance with the Unified Graphics System Law, an Image serves as the
// universal cross-backend texture primitive. Depending on the active Graphics
// backend (Graphics_getGraphicsId()), it carries:
//   - Vulkan: VkImage handle
//   - Metal: id<MTLTexture> handle (backed by an IOSurface)
//   - Direct3D 12: ID3D12Resource* handle
//   - Software / Raster: CPU-side RGBA8 shadow buffer
//
// BYTE ORDER: RGBA8 (byte 0 = RED, 1 = GREEN, 2 = BLUE, 3 = ALPHA), the
// monotonic memory layout of the Strict 0xRRGGBBAA Color Law.

typedef struct Image {
    uint32_t width;        // pixels across (>= 1)
    uint32_t height;       // pixels down (>= 1)
    uint32_t format;       // backend-agnostic pixel format code (0 = RGBA8 stub default)
    uint32_t usage;        // backend-agnostic usage flags (0 = none)
    uint64_t typeId;       // block-header type id (TYPE_IMAGE_SINGLETON)
    uint8_t *rgba;         // OWNED CPU shadow, width*height*4 bytes RGBA8
    void    *vkImage;      // VkImage handle (Vulkan backend)
    void    *metalTexture; // id<MTLTexture> handle (Metal backend)
    void    *d3d12Resource;// ID3D12Resource* handle (DirectX 12 backend)
    void    *ioSurface;    // IOSurfaceRef handle (zero-copy shared GPU memory)
} Image;

// Empty 1x1 placeholder (format 0, usage 0, zeroed pixel)
Image *Image_0(void);

// Sized image (format 0, usage 0, zeroed pixels); null on zero dims or OOM
Image *Image_2(uint32_t w, uint32_t h);

// Fully specified image (zeroed pixels); null on zero dims or OOM
Image *Image_4(uint32_t w, uint32_t h, uint32_t format, uint32_t usage);

// Release the owned shadow and struct (null-safe no-op)
void Image_free(Image *img);

// Copy w*h*4 RGBA8 bytes into dest (dest-last); grows/clears on dim change
bool Image_upload(const uint8_t *rgba, uint32_t w, uint32_t h, Image *dest);

// Unified GPU Handle Accessor (resolves per active Graphics backend)
void *Image_getGpuHandle(const Image *img);

// Symmetric mutators
void Image_setWidth(Image *img, uint32_t w);
void Image_setHeight(Image *img, uint32_t h);
void Image_setFormat(Image *img, uint32_t format);
void Image_setUsage(Image *img, uint32_t usage);
void Image_setVkImage(Image *img, void *vkImage);
void Image_setMetalTexture(Image *img, void *metalTexture);
void Image_setDirect12Resource(Image *img, void *d3d12Resource);
void Image_setIOSurface(Image *img, void *ioSurface);

// Null-safe inspectors
uint32_t Image_getWidth(const Image *img);
uint32_t Image_getHeight(const Image *img);
uint32_t Image_getFormat(const Image *img);
uint32_t Image_getUsage(const Image *img);
void    *Image_getVkImage(const Image *img);
void    *Image_getMetalTexture(const Image *img);
void    *Image_getDirect12Resource(const Image *img);
void    *Image_getIOSurface(const Image *img);

#define Image(...) CONSTRUCTOR_DISPATCH(Image, __VA_ARGS__)
#endif
