#include "c23/constructor.h"
#ifndef BUFFERS_STENCIL_BUFFER_H
#define BUFFERS_STENCIL_BUFFER_H

#include "buffer/buffer.h"

// buffers/stencil_buffer.h — 1-channel 8-bit stencil masking buffer.
// Ported from legacy buffers/StencilBuffer.java.

Buffer *StencilBuffer_2(size_t width, size_t height);
uint8_t StencilBuffer_get(const Buffer *buf, size_t x, size_t y);
void StencilBuffer_set(Buffer *buf, size_t x, size_t y, uint8_t val);


#define StencilBuffer(...) CONSTRUCTOR_DISPATCH(StencilBuffer, __VA_ARGS__)
#endif
