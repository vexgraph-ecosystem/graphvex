#include "buffer/stencil_buffer.h"

#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Stencil_buffer
 * ============================================================================
 * Single-channel 8-bit discrete masking and clipping buffer specialization of the
 * base Buffer engine. Allocates 64-bit aligned raster storage for integer mask values
 * used in clip volume determination, path rendering silhouettes, and multi-pass masking
 * in compliance with the Unified Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Stencil_buffer (buffer/stencil_buffer.c)
 * LEVEL: L2 — Behavior (raster buffer behavior API)
 * ============================================================================
 * 1-channel 8-bit stencil masking buffer.
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
 *   - StencilBuffer_2(width, height)      : Allocate 1-channel stencil buffer
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
 *   - StencilBuffer_set(buf, x, y, val)   : Store 8-bit stencil mask at pixel
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - StencilBuffer_get(buf, x, y)        : Retrieve 8-bit stencil mask at pixel
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *StencilBuffer_2(size_t width, size_t height) {
    return Buffer(ID_STENCIL_BUFFER, width, height, 1);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void StencilBuffer_set(Buffer *buf, size_t x, size_t y, uint8_t val) {
    Buffer_setPixel(buf, x, y, 0, val);
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
uint8_t StencilBuffer_get(const Buffer *buf, size_t x, size_t y) {
    return (uint8_t)Buffer_getPixel(buf, x, y, 0);
}

