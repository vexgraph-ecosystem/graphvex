#include "c23/constructor.h"
#ifndef BUFFERS_DEPTH_BUFFER_H
#define BUFFERS_DEPTH_BUFFER_H

#include "buffer/buffer.h"

// buffers/depth_buffer.h — 1-channel floating-point depth buffer.
// Ported from legacy buffers/DepthBuffer.java.

Buffer *DepthBuffer_2(size_t width, size_t height);
float DepthBuffer_get(const Buffer *buf, size_t x, size_t y);
void DepthBuffer_set(Buffer *buf, size_t x, size_t y, float depth);
void DepthBuffer_clear(Buffer *buf, float depth);


#define DepthBuffer(...) CONSTRUCTOR_DISPATCH(DepthBuffer, __VA_ARGS__)
#endif
