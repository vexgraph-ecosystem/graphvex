#include "buffer/color_buffer.h"

#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Color_buffer
 * ============================================================================
 * Four-channel RGBA raster color buffer specialization of the base Buffer engine.
 * Stores 8-bit discrete red, green, blue, and alpha color components across independent
 * raster channels in compliance with the Strict 0xRRGGBBAA Color Law.
 *
 * Provides dedicated whole-buffer color clearing and per-pixel color getter and setter
 * primitives for software rasterization, surface accumulation, and offscreen staging.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Color_buffer (buffer/color_buffer.c)
 * LEVEL: L2 — Behavior (raster buffer behavior API)
 * ============================================================================
 * 4-channel RGBA color buffer.
 *
 * STRUCT FIELDS (Mirroring buffer/buffer.h):
 * ----------------------------------------------------------------------------
 *   Buffer {
 *     uint32_t width; // raster width in pixels
 *     uint32_t height; // raster height in pixels
 *     uint32_t channels; // channel count (4)
 *     uint32_t typeId; // block-header type id
 *     uint32_t length; // width * height * channels
 *     uint32_t pad; // alignment padding
 *     uint64_t data[]; // Contiguous 64-bit element array
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - ColorBuffer_2(width, height)                : Allocate 4-channel RGBA buffer
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - ColorBuffer_clearRGBA(buf, r, g, b, a)      : Clear entire buffer to RGBA
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - ColorBuffer_setRGBA(buf, x, y, r, g, b, a)  : Store RGBA channels at pixel
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - ColorBuffer_getRGBA(buf, x, y, r, g, b, a)  : Retrieve RGBA channels at pixel
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *ColorBuffer_2(size_t width, size_t height) {
    return Buffer(ID_COLOR_BUFFER, width, height, 4);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void ColorBuffer_clearRGBA(Buffer *buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!buf) return;
    size_t w = Buffer_width(buf);
    size_t h = Buffer_height(buf);
    for (size_t y = 0; y < h; y++) {
        for (size_t x = 0; x < w; x++) {
            ColorBuffer_setRGBA(buf, x, y, r, g, b, a);
        }
    }
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void ColorBuffer_setRGBA(Buffer *buf, size_t x, size_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Buffer_setPixel(buf, x, y, 0, r);
    Buffer_setPixel(buf, x, y, 1, g);
    Buffer_setPixel(buf, x, y, 2, b);
    Buffer_setPixel(buf, x, y, 3, a);
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
void ColorBuffer_getRGBA(const Buffer *buf, size_t x, size_t y, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a) {
    if (r) *r = (uint8_t)Buffer_getPixel(buf, x, y, 0);
    if (g) *g = (uint8_t)Buffer_getPixel(buf, x, y, 1);
    if (b) *b = (uint8_t)Buffer_getPixel(buf, x, y, 2);
    if (a) *a = (uint8_t)Buffer_getPixel(buf, x, y, 3);
}

