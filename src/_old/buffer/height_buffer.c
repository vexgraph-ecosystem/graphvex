#include "buffer/height_buffer.h"

#include <string.h>

#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Height_buffer
 * ============================================================================
 * Single-channel scalar elevation and displacement buffer specialization of the base Buffer engine.
 * Encodes 32-bit floating-point elevation values into 64-bit raster units for terrain synthesis,
 * geometric tessellation displacement, and physical simulation in compliance with the Unified
 * Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Height_buffer (buffer/height_buffer.c)
 * LEVEL: L2 — Behavior (raster buffer behavior API)
 * ============================================================================
 * 1-channel terrain/displacement elevation buffer.
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
 *   - HeightBuffer_2(width, height)        : Allocate 1-channel height buffer
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - (none)
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - HeightBuffer_setHeight(buf, x, y, h) : Store float elevation at pixel
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - HeightBuffer_getHeight(buf, x, y)    : Retrieve float elevation at pixel
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *HeightBuffer_2(size_t width, size_t height) {
    return Buffer(ID_HEIGHT_BUFFER, width, height, 1);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void HeightBuffer_setHeight(Buffer *buf, size_t x, size_t y, float h) {
    uint64_t raw = 0;
    memcpy(&raw, &h, sizeof(float));
    Buffer_setPixel(buf, x, y, 0, raw);
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
float HeightBuffer_getHeight(const Buffer *buf, size_t x, size_t y) {
    uint64_t raw = Buffer_getPixel(buf, x, y, 0);
    float val;
    memcpy(&val, &raw, sizeof(float));
    return val;
}

