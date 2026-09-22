#include "c23/constructor.h"
#ifndef BUFFERS_COLOR_BUFFER_H
#define BUFFERS_COLOR_BUFFER_H

#include "buffer/buffer.h"

// buffers/color_buffer.h — 4-channel RGBA color buffer.
// Ported from legacy buffers/ColorBuffer.java.

Buffer *ColorBuffer_2(size_t width, size_t height);
void ColorBuffer_setRGBA(Buffer *buf, size_t x, size_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void ColorBuffer_getRGBA(const Buffer *buf, size_t x, size_t y, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a);
void ColorBuffer_clearRGBA(Buffer *buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a);


#define ColorBuffer(...) CONSTRUCTOR_DISPATCH(ColorBuffer, __VA_ARGS__)
#endif
