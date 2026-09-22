#include "buffer/depth_buffer.h"

#include <string.h>

#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Depth_buffer
 * ============================================================================
 * Single-channel floating-point depth buffer specialization of the base Buffer engine.
 * Encodes 32-bit single-precision floating-point Z-depth values directly into 64-bit
 * raster elements using lossless bitwise representation.
 *
 * Facilitates depth testing, occlusion culling, and Z-buffer clearing across software
 * rasterizer stages and hardware depth-pass emulation in compliance with the Unified
 * Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Depth_buffer (buffer/depth_buffer.c)
 * LEVEL: L2 — Behavior (raster buffer behavior API)
 * ============================================================================
 * 1-channel floating-point depth buffer.
 *
 * STRUCT FIELDS (Mirroring buffer/buffer.h):
 * ----------------------------------------------------------------------------
 *   Buffer {
 *     uint32_t width; // raster width in pixels
 *     uint32_t height; // raster height in pixels
 *     uint32_t channels; // channel count (1)
 *     uint32_t typeId; // block-header type id
 *     uint32_t length; // width * height * channels
 *     uint32_t pad; // alignment padding
 *     uint64_t data[]; // Contiguous 64-bit element array
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - DepthBuffer_2(width, height)     : Allocate 1-channel depth buffer
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - DepthBuffer_clear(buf, depth)    : Clear buffer to uniform depth float
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - DepthBuffer_set(buf, x, y, depth): Write float depth value at pixel
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - DepthBuffer_get(buf, x, y)       : Retrieve float depth value at pixel
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *DepthBuffer_2(size_t width, size_t height) {
    return Buffer(ID_DEPTH_BUFFER, width, height, 1);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

void DepthBuffer_clear(Buffer *buf, float depth) {
    uint64_t raw = 0;
    memcpy(&raw, &depth, sizeof(float));
    Buffer_clear(buf, raw);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void DepthBuffer_set(Buffer *buf, size_t x, size_t y, float depth) {
    uint64_t raw = 0;
    memcpy(&raw, &depth, sizeof(float));
    Buffer_setPixel(buf, x, y, 0, raw);
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
float DepthBuffer_get(const Buffer *buf, size_t x, size_t y) {
    uint64_t raw = Buffer_getPixel(buf, x, y, 0);
    float val;
    memcpy(&val, &raw, sizeof(float));
    return val;
}

