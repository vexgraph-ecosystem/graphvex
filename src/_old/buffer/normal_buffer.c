#include "buffer/normal_buffer.h"

#include <string.h>

#include "oop/type.h"
#include "annotation/definition.h"
#include "annotation/overview.h"
#include "annotation/getter.h"
#include "annotation/setter.h"

;;DEFINITION
/**
 * ============================================================================
 * DEFINITION: Normal_buffer
 * ============================================================================
 * Three-channel (XYZ) surface and view-space normal vector buffer specialization
 * of the base Buffer engine. Encodes normalized floating-point components across
 * three 64-bit raster channels using IEEE-754 bit representations.
 *
 * Utilized by deferred shading passes, lighting calculation pipelines, and normal
 * map staging stages in compliance with the Unified Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Normal_buffer (buffer/normal_buffer.c)
 * LEVEL: L2 — Behavior (raster buffer behavior API)
 * ============================================================================
 * 3-channel (XYZ) surface/view normals buffer.
 *
 * STRUCT FIELDS (Mirroring buffer/buffer.h):
 * ----------------------------------------------------------------------------
 *   Buffer {
 *     uint32_t width; // raster width in pixels
 *     uint32_t height; // raster height in pixels
 *     uint32_t channels; // channel count (3)
 *     uint32_t typeId; // block-header type id
 *     uint32_t length; // width * height * channels
 *     uint32_t pad; // alignment padding
 *     uint64_t data[]; // Contiguous 64-bit element array
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - NormalBuffer_2(width, height)                     : Allocate 3-channel normal buffer
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
 *   - NormalBuffer_setNormal(buf, x, y, nx, ny, nz)     : Store normal XYZ floats at pixel
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - NormalBuffer_getNormal(buf, x, y, nx, ny, nz)     : Retrieve normal XYZ floats at pixel
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *NormalBuffer_2(size_t width, size_t height) {
    return Buffer(ID_NORMAL_BUFFER, width, height, 3);
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

// (none)

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void NormalBuffer_setNormal(Buffer *buf, size_t x, size_t y, float nx, float ny, float nz) {
    uint64_t rx = 0, ry = 0, rz = 0;
    memcpy(&rx, &nx, sizeof(float));
    memcpy(&ry, &ny, sizeof(float));
    memcpy(&rz, &nz, sizeof(float));
    Buffer_setPixel(buf, x, y, 0, rx);
    Buffer_setPixel(buf, x, y, 1, ry);
    Buffer_setPixel(buf, x, y, 2, rz);
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
void NormalBuffer_getNormal(const Buffer *buf, size_t x, size_t y, float *nx, float *ny, float *nz) {
    uint64_t rx = Buffer_getPixel(buf, x, y, 0);
    uint64_t ry = Buffer_getPixel(buf, x, y, 1);
    uint64_t rz = Buffer_getPixel(buf, x, y, 2);
    if (nx) memcpy(nx, &rx, sizeof(float));
    if (ny) memcpy(ny, &ry, sizeof(float));
    if (nz) memcpy(nz, &rz, sizeof(float));
}

