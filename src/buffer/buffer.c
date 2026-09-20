#include "buffer/buffer.h"

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
 * DEFINITION: Buffer
 * ============================================================================
 * Core off-heap 2D multi-channel raster buffer memory management engine.
 * Wraps contiguous 64-bit element arrays tagged with block-header type metadata
 * across width, height, and channel dimensions.
 *
 * Provides safe dynamic memory reallocation, sub-region blitting, memory copying,
 * and bilinear interpolation sampling for 2D graphics rasterization, texture staging,
 * and multi-pass composition in compliance with the Unified Graphics Abstraction Law.
 * ============================================================================
 */

;;OVERVIEW
/**
 * ============================================================================
 * CLASS: Buffer (buffer/buffer.c)
 * LEVEL: L2 — Behavior (raster buffer behavior API)
 * ============================================================================
 * Core 2D multi-channel raster buffer engine.
 *
 * STRUCT FIELDS (Mirroring buffer/buffer.h):
 * ----------------------------------------------------------------------------
 *   Buffer {
 *     uint32_t width; // raster width in pixels
 *     uint32_t height; // raster height in pixels
 *     uint32_t channels; // channel count
 *     uint32_t pad0; // explicit alignment before the 64-bit id
 *     uint64_t typeId; // block-header type id
 *     uint32_t length; // width * height * channels
 *     uint32_t pad; // alignment padding
 *     uint64_t data[]; // Contiguous 64-bit element array
 *   }
 *
 * FUNCTION REGISTRY:
 * ----------------------------------------------------------------------------
 * Public Constructors: (.h)
 *   - Buffer_4(classId, width, height, channels) : Allocate multi-channel buffer
 *
 * Private Constructors: (.c static)
 *   - (none)
 *
 * Public Core Functions: (.h)
 *   - Buffer_expand(buf, newWidth, newHeight)    : Expand buffer preserving content
 *   - Buffer_free(buf)                           : Release buffer heap memory
 *   - Buffer_clear(buf, clearValue)              : Fill buffer with value
 *   - Buffer_copy(src, dst)                      : Direct buffer memory copy
 *   - Buffer_blit(src, dst, ...)                 : Sub-region blit transfer
 *   - Buffer_sample(buf, u, v, channel)          : Bilinear channel sampling
 *
 * Private Core Functions: (.c static)
 *   - (none)
 *
 * Public Setters: (.h)
 *   - Buffer_set(buf, index, value)              : Linear index value assignment
 *   - Buffer_setPixel(buf, x, y, channel, value) : 2D coordinate value assignment
 *
 * Private Setters: (.c static)
 *   - (none)
 *
 * Public Getters: (.h)
 *   - Buffer_width(buf)                          : Query raster pixel width
 *   - Buffer_height(buf)                         : Query raster pixel height
 *   - Buffer_channels(buf)                       : Query raster channel count
 *   - Buffer_length(buf)                         : Query total element count
 *   - Buffer_type(buf)                           : Query type ID descriptor
 *   - Buffer_classId(buf)                        : Query class identifier
 *   - Buffer_data(buf)                           : Query mutable element array pointer
 *   - Buffer_constData(buf)                      : Query immutable element array pointer
 *   - Buffer_get(buf, index)                     : Query linear index element value
 *   - Buffer_getPixel(buf, x, y, channel)        : Query 2D coordinate channel value
 *
 * Private Getters: (.c static)
 *   - (none)
 * ============================================================================
 */

// ============================================================================
// CONSTRUCTORS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *Buffer_4(uint32_t classId, size_t width, size_t height, size_t channels) {
    if (width == 0 || height == 0 || channels == 0)
        return nullptr;

    size_t length = width * height * channels;
    uint64_t type = Type_make(PROJ_VEXSPOKE, FORM_ARRAY, classId);
    size_t payloadBytes = sizeof(Buffer) + length * sizeof(uint64_t);

    Buffer *buf = Memory_alloc(type, payloadBytes);
    if (!buf)
        return nullptr;

    (*buf).width = (uint32_t)width;
    (*buf).height = (uint32_t)height;
    (*buf).channels = (uint32_t)channels;
    (*buf).typeId = type;
    (*buf).length = (uint32_t)length;
    (*buf).pad = 0;

    memset((*buf).data, 0, length * sizeof(uint64_t));
    return buf;
}

// ============================================================================
// CORE FUNCTIONS (PUBLIC & PRIVATE)
// ============================================================================

Buffer *Buffer_expand(Buffer *buf, size_t newWidth, size_t newHeight) {
    if (!buf) return nullptr;
    uint32_t cid = Buffer_classId(buf);
    size_t channels = (*buf).channels;
    Buffer *newBuf = Buffer_4(cid, newWidth, newHeight, channels);
    if (!newBuf) return nullptr;

    size_t copyW = (*buf).width < newWidth ? (*buf).width : newWidth;
    size_t copyH = (*buf).height < newHeight ? (*buf).height : newHeight;

    for (size_t y = 0; y < copyH; y++) {
        for (size_t x = 0; x < copyW; x++) {
            for (size_t c = 0; c < channels; c++) {
                uint64_t val = Buffer_getPixel(buf, x, y, c);
                Buffer_setPixel(newBuf, x, y, c, val);
            }
        }
    }

    Buffer_free(buf);
    return newBuf;
}

void Buffer_free(Buffer *buf) {
    if (!buf) return;
    Memory_free(buf);
}

void Buffer_clear(Buffer *buf, uint64_t clearValue) {
    if (!buf) return;
    size_t len = (*buf).length;
    for (size_t i = 0; i < len; i++) {
        (*buf).data[i] = clearValue;
    }
}

void Buffer_copy(const Buffer *src, Buffer *dst) {
    if (!src || !dst) return;
    if ((*src).width != (*dst).width || (*src).height != (*dst).height || (*src).channels != (*dst).channels)
        return;
    memcpy((*dst).data, (*src).data, (*src).length * sizeof(uint64_t));
}

void Buffer_blit(const Buffer *src, Buffer *dst, size_t srcX, size_t srcY, size_t dstX, size_t dstY, size_t width, size_t height) {
    if (!src || !dst) return;
    size_t channels = (*src).channels < (*dst).channels ? (*src).channels : (*dst).channels;

    for (size_t dy = 0; dy < height; dy++) {
        size_t sy = srcY + dy;
        size_t ty = dstY + dy;
        if (sy >= (*src).height || ty >= (*dst).height)
            continue;

        for (size_t dx = 0; dx < width; dx++) {
            size_t sx = srcX + dx;
            size_t tx = dstX + dx;
            if (sx >= (*src).width || tx >= (*dst).width)
                continue;

            for (size_t c = 0; c < channels; c++) {
                uint64_t val = Buffer_getPixel(src, sx, sy, c);
                Buffer_setPixel(dst, tx, ty, c, val);
            }
        }
    }
}

float Buffer_sample(const Buffer *buf, float u, float v, size_t channel) {
    if (!buf || (*buf).width == 0 || (*buf).height == 0 || channel >= (*buf).channels)
        return 0.0f;

    // Clamp u, v to [0.0, 1.0]
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;

    float fx = u * (float)((*buf).width - 1);
    float fy = v * (float)((*buf).height - 1);

    size_t x0 = (size_t)fx;
    size_t y0 = (size_t)fy;
    size_t x1 = (x0 + 1 < (*buf).width) ? x0 + 1 : x0;
    size_t y1 = (y0 + 1 < (*buf).height) ? y0 + 1 : y0;

    float tx = fx - (float)x0;
    float ty = fy - (float)y0;

    float v00 = (float)Buffer_getPixel(buf, x0, y0, channel);
    float v10 = (float)Buffer_getPixel(buf, x1, y0, channel);
    float v01 = (float)Buffer_getPixel(buf, x0, y1, channel);
    float v11 = (float)Buffer_getPixel(buf, x1, y1, channel);

    float top = v00 + tx * (v10 - v00);
    float bot = v01 + tx * (v11 - v01);
    return top + ty * (bot - top);
}

// ============================================================================
// SETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;SETTER
void Buffer_set(Buffer *buf, size_t index, uint64_t value) {
    if (!buf || index >= (*buf).length)
        return;
    (*buf).data[index] = value;
}

;;SETTER
void Buffer_setPixel(Buffer *buf, size_t x, size_t y, size_t channel, uint64_t value) {
    if (!buf || x >= (*buf).width || y >= (*buf).height || channel >= (*buf).channels)
        return;
    size_t idx = (y * (*buf).width + x) * (*buf).channels + channel;
    (*buf).data[idx] = value;
}

// ============================================================================
// GETTERS (PUBLIC & PRIVATE)
// ============================================================================

;;GETTER
size_t Buffer_width(const Buffer *buf) {
    return buf ? (*buf).width : 0;
}

;;GETTER
size_t Buffer_height(const Buffer *buf) {
    return buf ? (*buf).height : 0;
}

;;GETTER
size_t Buffer_channels(const Buffer *buf) {
    return buf ? (*buf).channels : 0;
}

;;GETTER
size_t Buffer_length(const Buffer *buf) {
    return buf ? (*buf).length : 0;
}

;;GETTER
uint64_t Buffer_type(const Buffer *buf) {
    return buf ? (*buf).typeId : 0;
}

;;GETTER
uint32_t Buffer_classId(const Buffer *buf) {
    return buf ? Type_class((*buf).typeId) : 0;
}

;;GETTER
uint64_t *Buffer_data(Buffer *buf) {
    return buf ? (*buf).data : nullptr;
}

;;GETTER
const uint64_t *Buffer_constData(const Buffer *buf) {
    return buf ? (*buf).data : nullptr;
}

;;GETTER
uint64_t Buffer_get(const Buffer *buf, size_t index) {
    if (!buf || index >= (*buf).length)
        return 0;
    return (*buf).data[index];
}

;;GETTER
uint64_t Buffer_getPixel(const Buffer *buf, size_t x, size_t y, size_t channel) {
    if (!buf || x >= (*buf).width || y >= (*buf).height || channel >= (*buf).channels)
        return 0;
    size_t idx = (y * (*buf).width + x) * (*buf).channels + channel;
    return (*buf).data[idx];
}
