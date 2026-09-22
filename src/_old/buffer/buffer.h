#ifndef BUFFERS_BUFFER_H
#define BUFFERS_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "c23/constructor.h"

// buffers/buffer.h — Core 2D multi-channel raster buffer engine.
// Ported from legacy buffers/Buffer.java.
//
// A Buffer is an off-heap multi-channel 2D raster memory block (width * height * channels).
// It carries a 32-byte header metadata block (width, height, channels, typeId, length).

typedef struct Buffer {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t pad0;
    uint64_t typeId;
    uint32_t length;     // width * height * channels
    uint32_t pad;
    uint64_t data[];     // Contiguous 64-bit element array
} Buffer;

// Allocate a buffer of (width * height * channels) elements
Buffer *Buffer_4(uint32_t classId, size_t width, size_t height, size_t channels);

// Expand an existing buffer to new dimensions, preserving overlapping content
Buffer *Buffer_expand(Buffer *buf, size_t newWidth, size_t newHeight);

// Free buffer memory
void Buffer_free(Buffer *buf);

// Dimension and metadata inspectors
size_t   Buffer_width(const Buffer *buf);
size_t   Buffer_height(const Buffer *buf);
size_t   Buffer_channels(const Buffer *buf);
size_t   Buffer_length(const Buffer *buf);
uint64_t Buffer_type(const Buffer *buf);
uint32_t Buffer_classId(const Buffer *buf);
uint64_t *Buffer_data(Buffer *buf);
const uint64_t *Buffer_constData(const Buffer *buf);

// Element accessors (linear index)
uint64_t Buffer_get(const Buffer *buf, size_t index);
void     Buffer_set(Buffer *buf, size_t index, uint64_t value);

// 2D Pixel / Channel accessors
uint64_t Buffer_getPixel(const Buffer *buf, size_t x, size_t y, size_t channel);
void     Buffer_setPixel(Buffer *buf, size_t x, size_t y, size_t channel, uint64_t value);

// Operations: clear, copy, blit, bilinear sampling
void Buffer_clear(Buffer *buf, uint64_t clearValue);
void Buffer_copy(const Buffer *src, Buffer *dst);
void Buffer_blit(const Buffer *src, Buffer *dst, size_t srcX, size_t srcY, size_t dstX, size_t dstY, size_t width, size_t height);
float Buffer_sample(const Buffer *buf, float u, float v, size_t channel);


#define Buffer(...) CONSTRUCTOR_DISPATCH(Buffer, __VA_ARGS__)
#endif
