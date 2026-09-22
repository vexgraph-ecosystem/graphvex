#include "c23/constructor.h"
#ifndef BUFFERS_HEIGHT_BUFFER_H
#define BUFFERS_HEIGHT_BUFFER_H

#include "buffer/buffer.h"

// buffers/height_buffer.h — 1-channel terrain/displacement elevation buffer.
// Ported from legacy buffers/HeightBuffer.java.

Buffer *HeightBuffer_2(size_t width, size_t height);
float HeightBuffer_getHeight(const Buffer *buf, size_t x, size_t y);
void HeightBuffer_setHeight(Buffer *buf, size_t x, size_t y, float h);


#define HeightBuffer(...) CONSTRUCTOR_DISPATCH(HeightBuffer, __VA_ARGS__)
#endif
